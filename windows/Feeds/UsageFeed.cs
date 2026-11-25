using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Text;
using System.Text.Json.Nodes;

namespace WaveshareLink.Feeds;

/// "Uso": Claude, Codex and Antigravity usage, the same sources as the Mac app (UsageProviders.swift).
/// Fetched while this PC is on the board's screen with the page enabled. The APIs are rate limited, so:
///   - automatic fetches happen at most every 5 min, counted from the last fetch: switching pages or
///     computers does not restart the count (coming back early just resends what we have);
///   - the board's refresh button fetches at most once a minute (earlier presses resend what we have).
///   {"t":"usage","providers":[{"name","pct","err","busy","at","rows":[{"sec","label","pct","reset"}]}]}
sealed class UsageFeed
{
    static readonly TimeSpan AutoInterval = TimeSpan.FromMinutes(5);
    static readonly TimeSpan ManualInterval = TimeSpan.FromMinutes(1);

    public Action? OnChange;
    readonly Entry[] entries = [new(new ClaudeProvider()), new(new CodexProvider()), new(new AntigravityProvider())];
    // Checks whether the 5 min are up; the deadline itself comes from lastFetch.
    readonly System.Windows.Forms.Timer timer = new() { Interval = 15_000 };
    DateTime lastFetch = DateTime.MinValue;

    public UsageFeed()
    {
        timer.Tick += (_, _) =>
        {
            if (DateTime.Now - lastFetch >= AutoInterval) Fetch();
        };
    }

    public void SetActive(bool on)
    {
        if (on && !timer.Enabled)
        {
            timer.Start();
            if (DateTime.Now - lastFetch >= AutoInterval) Fetch();
        }
        else if (!on)
        {
            timer.Stop();
        }
    }

    /// The board's refresh button.
    public void Refresh()
    {
        var since = DateTime.Now - lastFetch;
        if (since < ManualInterval)
        {
            Log.Info($"Uso: atualizado há {(int)since.TotalSeconds} s; próxima atualização manual liberada em {(int)(ManualInterval - since).TotalSeconds} s.");
            OnChange?.Invoke(); // the same data, not busy: releases the board's button
            return;
        }
        Fetch();
    }

    void Fetch()
    {
        lastFetch = DateTime.Now;
        foreach (var e in entries.Where(e => !e.Busy)) _ = FetchAsync(e);
        OnChange?.Invoke(); // the board shows "Atualizando..." right away
    }

    async Task FetchAsync(Entry e)
    {
        e.Busy = true;
        try
        {
            e.Rows = await e.Provider.FetchAsync();
            e.UpdatedAt = DateTime.Now;
            e.Error = null;
        }
        catch (UsageException x)
        {
            if (x.Kind == UsageError.NotRunning) e.Rows = [];
            e.Error = x.Kind switch
            {
                UsageError.NotRunning => e.Provider.NoCredentialsMessage,
                UsageError.Expired => e.Provider.ExpiredMessage,
                UsageError.NoCredentials => e.Provider.NoCredentialsMessage,
                UsageError.Http => $"Erro HTTP {x.Status}",
                UsageError.Network => $"Sem conexão: {x.Message}",
                _ => "Resposta inesperada da API",
            };
            Log.Info($"Uso ({e.Provider.Name}): {e.Error}");
        }
        catch (Exception x)
        {
            e.Error = "Resposta inesperada da API";
            Log.Info($"Uso ({e.Provider.Name}): {x.Message}");
        }
        e.Busy = false;
        OnChange?.Invoke();
    }

    public JsonObject Message => new()
    {
        ["t"] = "usage",
        ["providers"] = Json.Array(entries.Select(e => (JsonNode?)new JsonObject
        {
            ["name"] = e.Provider.Name,
            ["pct"] = e.Provider.Headline(e.Rows) is double h ? (int)Math.Round(h) : -1,
            ["err"] = Text.Ascii(e.Error),
            ["busy"] = e.Busy,
            ["at"] = e.UpdatedAt?.ToString("HH:mm") ?? "",
            ["rows"] = Json.Array(e.Rows.Select(r => (JsonNode?)new JsonObject
            {
                ["sec"] = Text.Ascii(r.Section),
                ["label"] = Text.Ascii(r.Label),
                ["pct"] = (int)Math.Round(r.UsedPct),
                ["reset"] = Text.Ascii(ShortReset(r.ResetsAt)),
            })),
        })),
    };

    static readonly CultureInfo PtBr = new("pt-BR");

    /// "14:30" today, "sex. 14:30" on other days (like the Mac).
    static string ShortReset(DateTime? d)
    {
        if (d is not DateTime t) return "";
        var local = t.ToLocalTime();
        return local.Date == DateTime.Today ? local.ToString("HH:mm") : local.ToString("ddd", PtBr) + ". " + local.ToString("HH:mm");
    }

    sealed class Entry(IUsageProvider provider)
    {
        public readonly IUsageProvider Provider = provider;
        public List<UsageRow> Rows = [];
        public string? Error;
        public DateTime? UpdatedAt;
        public bool Busy;
    }
}

record UsageRow(string? Section, string Label, double UsedPct, DateTime? ResetsAt);

enum UsageError { NoCredentials, Expired, NotRunning, Http, BadResponse, Network }

sealed class UsageException(UsageError kind, string message = "", int status = 0) : Exception(message)
{
    public UsageError Kind { get; } = kind;
    public int Status { get; } = status;
}

interface IUsageProvider
{
    string Name { get; }
    string ExpiredMessage { get; }
    string NoCredentialsMessage { get; }
    Task<List<UsageRow>> FetchAsync();
    double? Headline(List<UsageRow> rows);
}

static class UsageHttp
{
    public static readonly HttpClient Client = new() { Timeout = TimeSpan.FromSeconds(20) };

    public static async Task<JsonObject> SendAsync(HttpRequestMessage req)
    {
        HttpResponseMessage resp;
        try
        {
            resp = await Client.SendAsync(req);
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException)
        {
            throw new UsageException(UsageError.Network, e.Message);
        }
        using (resp)
        {
            if (resp.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)
                throw new UsageException(UsageError.Expired);
            if (resp.StatusCode != HttpStatusCode.OK)
                throw new UsageException(UsageError.Http, status: (int)resp.StatusCode);
            try
            {
                return JsonNode.Parse(await resp.Content.ReadAsStringAsync()) as JsonObject
                       ?? throw new UsageException(UsageError.BadResponse);
            }
            catch (System.Text.Json.JsonException)
            {
                throw new UsageException(UsageError.BadResponse);
            }
        }
    }

    public static DateTime? ParseDate(string? s) =>
        DateTimeOffset.TryParse(s, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var d) ? d.UtcDateTime : null;

    public static double? Number(JsonNode? n) =>
        n is JsonValue v && v.TryGetValue(out double d) ? d : null;
}

/// Claude: the Claude Code login (on Windows, %USERPROFILE%\.claude\.credentials.json).
sealed class ClaudeProvider : IUsageProvider
{
    public string Name => "Claude";
    public string ExpiredMessage => "Login expirado: abra o Claude Code para renovar";
    public string NoCredentialsMessage => "Credenciais do Claude Code não encontradas";

    static readonly string[] Order = ["five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"];

    static string ReadAccessToken()
    {
        var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".claude", ".credentials.json");
        JsonObject? oauth;
        try
        {
            oauth = JsonNode.Parse(File.ReadAllText(path))?["claudeAiOauth"] as JsonObject;
        }
        catch (Exception)
        {
            throw new UsageException(UsageError.NoCredentials);
        }
        if (oauth == null || Json.Str(oauth, "accessToken") is not string token) throw new UsageException(UsageError.NoCredentials);
        if (UsageHttp.Number(oauth["expiresAt"]) is double exp && exp / 1000 < DateTimeOffset.UtcNow.ToUnixTimeSeconds())
            throw new UsageException(UsageError.Expired);
        return token;
    }

    static string Label(string key) => key switch
    {
        "five_hour" => "Sessão (5h)",
        "seven_day" => "Semana",
        "seven_day_opus" => "Semana (Opus)",
        "seven_day_sonnet" => "Semana (Sonnet)",
        _ => CultureInfo.InvariantCulture.TextInfo.ToTitleCase(key.Replace('_', ' ')),
    };

    public async Task<List<UsageRow>> FetchAsync()
    {
        var token = ReadAccessToken();
        using var req = new HttpRequestMessage(HttpMethod.Get, "https://api.anthropic.com/api/oauth/usage");
        req.Headers.Add("Authorization", $"Bearer {token}");
        req.Headers.Add("anthropic-beta", "oauth-2025-04-20");
        var json = await UsageHttp.SendAsync(req);

        var items = new List<(string Key, double Util, DateTime? Reset)>();
        foreach (var (key, value) in json)
        {
            if (value is not JsonObject o || UsageHttp.Number(o["utilization"]) is not double util) continue;
            items.Add((key, util, UsageHttp.ParseDate(Json.Str(o, "resets_at"))));
        }
        return items
            .OrderBy(i => Array.IndexOf(Order, i.Key) is int n && n >= 0 ? n : 99)
            .ThenBy(i => i.Key, StringComparer.Ordinal)
            .Select(i => new UsageRow(null, Label(i.Key), i.Util, i.Reset))
            .ToList();
    }

    public double? Headline(List<UsageRow> rows) => (rows.FirstOrDefault(r => r.Label == "Sessão (5h)") ?? rows.FirstOrDefault())?.UsedPct;
}

/// Antigravity: its local hub API (%USERPROFILE%\.gemini\antigravity\daemon\hub_*.json).
sealed class AntigravityProvider : IUsageProvider
{
    public string Name => "Antigravity";
    public string ExpiredMessage => "Antigravity recusou o acesso: reabra o Antigravity";
    public string NoCredentialsMessage => "Antigravity não está aberto";

    static (int Port, string Csrf) FindHub()
    {
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".gemini", "antigravity", "daemon");
        if (!Directory.Exists(dir)) throw new UsageException(UsageError.NotRunning);
        foreach (var file in new DirectoryInfo(dir).GetFiles("hub_*.json").OrderByDescending(f => f.LastWriteTimeUtc))
        {
            try
            {
                if (JsonNode.Parse(File.ReadAllText(file.FullName)) is not JsonObject j) continue;
                if (UsageHttp.Number(j["pid"]) is not double pid || UsageHttp.Number(j["httpPort"]) is not double port) continue;
                if (Json.Str(j, "csrfToken") is not string csrf) continue;
                if (!IsRunning((int)pid)) continue;
                return ((int)port, csrf);
            }
            catch (Exception)
            {
                // half-written or stale file
            }
        }
        throw new UsageException(UsageError.NotRunning);
    }

    static bool IsRunning(int pid)
    {
        try
        {
            using var p = Process.GetProcessById(pid);
            return !p.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }

    static string WindowLabel(string? w) => w switch
    {
        "5h" => "Sessão (5h)",
        "weekly" => "Semana",
        null => "Limite",
        _ => w,
    };

    public async Task<List<UsageRow>> FetchAsync()
    {
        var hub = FindHub();
        using var req = new HttpRequestMessage(HttpMethod.Post,
            $"http://127.0.0.1:{hub.Port}/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary")
        {
            Content = new StringContent("{}", Encoding.UTF8, "application/json"),
        };
        req.Headers.Add("Connect-Protocol-Version", "1");
        req.Headers.Add("x-codeium-csrf-token", hub.Csrf);

        JsonObject json;
        try
        {
            json = await UsageHttp.SendAsync(req);
        }
        catch (UsageException e) when (e.Kind == UsageError.Network)
        {
            throw new UsageException(UsageError.NotRunning);
        }
        if (json["response"]?["groups"] is not JsonArray groups) throw new UsageException(UsageError.BadResponse);

        var rows = new List<UsageRow>();
        foreach (var g in groups.OfType<JsonObject>())
        {
            var section = Json.Str(g, "displayName");
            if (g["buckets"] is not JsonArray buckets) continue;
            var sectionRows = new List<UsageRow>();
            foreach (var b in buckets.OfType<JsonObject>())
            {
                if (UsageHttp.Number(b["remainingFraction"]) is not double remaining) continue;
                sectionRows.Add(new UsageRow(section, WindowLabel(Json.Str(b, "window")),
                    Math.Clamp((1 - remaining) * 100, 0, 100), UsageHttp.ParseDate(Json.Str(b, "resetTime"))));
            }
            rows.AddRange(sectionRows.OrderBy(r => r.Label == "Sessão (5h)" || r.Label.Contains("5h") ? 0
                : r.Label == "Semana" ? 1 : 2));
        }
        return rows;
    }

    public double? Headline(List<UsageRow> rows) =>
        (rows.FirstOrDefault(r => r.Label == "Semana") ?? rows.FirstOrDefault())?.UsedPct;
}

/// Codex: the ChatGPT login of the Codex CLI / app (%USERPROFILE%\.codex\auth.json, or %CODEX_HOME%),
/// asked to the ChatGPT backend like the CLI's /status. If that answer is not understood, falls back to
/// the last "rate_limits" event Codex wrote in its session logs (.codex\sessions).
sealed class CodexProvider : IUsageProvider
{
    public string Name => "Codex";
    public string ExpiredMessage => "Login expirado: abra o Codex para renovar";
    public string NoCredentialsMessage => "Login do Codex não encontrado";

    static string Home =>
        Environment.GetEnvironmentVariable("CODEX_HOME") is { Length: > 0 } h
            ? h
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".codex");

    static (string Token, string? Account) ReadAuth()
    {
        JsonObject? tokens;
        try
        {
            tokens = JsonNode.Parse(File.ReadAllText(Path.Combine(Home, "auth.json")))?["tokens"] as JsonObject;
        }
        catch (Exception)
        {
            throw new UsageException(UsageError.NoCredentials);
        }
        if (tokens == null || Json.Str(tokens, "access_token") is not { Length: > 0 } token) throw new UsageException(UsageError.NoCredentials);
        return (token, Json.Str(tokens, "account_id"));
    }

    static string WindowLabel(double? minutes) => minutes switch
    {
        null or <= 0 => "Limite",
        < 6 * 60 => $"Sessão ({(int)Math.Round(minutes.Value / 60)}h)",
        < 2 * 24 * 60 => "Dia",
        < 8 * 24 * 60 => "Semana",
        _ => "Mês",
    };

    static DateTime? FromUnix(double? s) => s is double v ? DateTimeOffset.FromUnixTimeSeconds((long)v).UtcDateTime : null;

    /// API: {"rate_limit":{"primary_window":{"used_percent","limit_window_seconds","reset_at"},"secondary_window":...}}
    static List<UsageRow>? RowsFromApi(JsonObject json)
    {
        if (json["rate_limit"] is not JsonObject rl) return null;
        var rows = new List<UsageRow>();
        foreach (var key in new[] { "primary_window", "secondary_window" })
        {
            if (rl[key] is not JsonObject w || UsageHttp.Number(w["used_percent"]) is not double used) continue;
            var reset = FromUnix(UsageHttp.Number(w["reset_at"]))
                        ?? (UsageHttp.Number(w["reset_after_seconds"]) is double after ? DateTime.UtcNow.AddSeconds(after) : null);
            rows.Add(new UsageRow(null, WindowLabel(UsageHttp.Number(w["limit_window_seconds"]) / 60), Math.Clamp(used, 0, 100), reset));
        }
        return rows.Count > 0 ? rows : null;
    }

    /// Session log: {"payload":{"rate_limits":{"primary":{"used_percent","window_minutes","resets_at"},"secondary":...}}}
    static List<UsageRow>? RowsFromSessions()
    {
        var dir = Path.Combine(Home, "sessions");
        if (!Directory.Exists(dir)) return null;
        var files = new DirectoryInfo(dir).EnumerateFiles("*.jsonl", SearchOption.AllDirectories)
            .OrderByDescending(f => f.LastWriteTimeUtc).Take(5);
        foreach (var file in files)
        {
            string text;
            try
            {
                using var fs = new FileStream(file.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                fs.Seek(Math.Max(0, fs.Length - 512_000), SeekOrigin.Begin);
                text = new StreamReader(fs, Encoding.UTF8).ReadToEnd();
            }
            catch (IOException)
            {
                continue;
            }
            foreach (var line in text.Split('\n').Reverse())
            {
                if (!line.Contains("\"rate_limits\"")) continue;
                JsonObject? rl;
                try
                {
                    var obj = JsonNode.Parse(line);
                    rl = (obj?["payload"]?["rate_limits"] ?? obj?["rate_limits"]) as JsonObject;
                }
                catch (System.Text.Json.JsonException)
                {
                    continue; // the cut first line
                }
                if (rl == null) continue;
                var rows = new List<UsageRow>();
                foreach (var key in new[] { "primary", "secondary" })
                {
                    if (rl[key] is not JsonObject w || UsageHttp.Number(w["used_percent"]) is not double used) continue;
                    var label = WindowLabel(UsageHttp.Number(w["window_minutes"]));
                    var reset = FromUnix(UsageHttp.Number(w["resets_at"]));
                    rows.Add(reset < DateTime.UtcNow
                        ? new UsageRow(null, label, 0, null) // that window already reset
                        : new UsageRow(null, label, Math.Clamp(used, 0, 100), reset));
                }
                if (rows.Count > 0) return rows;
            }
        }
        return null;
    }

    public async Task<List<UsageRow>> FetchAsync()
    {
        var auth = ReadAuth();
        using var req = new HttpRequestMessage(HttpMethod.Get, "https://chatgpt.com/backend-api/wham/usage");
        req.Headers.Add("Authorization", $"Bearer {auth.Token}");
        if (auth.Account != null) req.Headers.Add("ChatGPT-Account-Id", auth.Account);
        req.Headers.Add("originator", "codex_cli_rs");
        try
        {
            var json = await UsageHttp.SendAsync(req);
            return RowsFromApi(json) ?? RowsFromSessions() ?? throw new UsageException(UsageError.BadResponse);
        }
        catch (UsageException e) when (e.Kind is UsageError.Http or UsageError.BadResponse)
        {
            return RowsFromSessions() ?? throw e;
        }
    }

    public double? Headline(List<UsageRow> rows) =>
        (rows.FirstOrDefault(r => r.Label.StartsWith("Sessão")) ?? rows.FirstOrDefault())?.UsedPct;
}

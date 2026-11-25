using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace WaveshareLink;

/// One button on the Atalhos or Apps page. Kind:
///   "builtin"    Value = shot, mic, sleep, lock, voldown, volup, mute, play, prev, next
///   "app"        Value = program to open (path, or a name Windows knows, e.g. "wt.exe")
///   "url"        Value = address or protocol (https://…, spotify:, discord://, ms-settings:)
///   "command"    Value = command line run hidden by cmd.exe
///   "powershell" Value = path of a .ps1 script
public sealed class KeyItem
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N")[..8];
    public string Text { get; set; } = "";
    public string Icon { get; set; } = "bullet";
    public string Kind { get; set; } = "builtin";
    public string Value { get; set; } = "";

    /// The id the board sends back ("key:<id>" / "app:<id>"). Built-ins keep their own id, so the
    /// board can tell which key is the mic.
    [JsonIgnore] public string BoardId => Kind == "builtin" ? Value : Id;

    public KeyItem() { }

    public KeyItem(string text, string icon, string kind, string value, string? id = null)
    {
        Text = text;
        Icon = icon;
        Kind = kind;
        Value = value;
        if (id != null) Id = id;
    }
}

/// The settings, in %APPDATA%\WaveshareLink\settings.json (also the token from pairing).
public sealed class Config
{
    public static string Dir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "WaveshareLink");
    public static string FilePath => Path.Combine(Dir, "settings.json");

    static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// Page ids the board knows, with their names, in the board's default order.
    public static readonly (string Id, string Name)[] AllPages =
    [
        ("usage", "Uso (Claude, Codex e Antigravity)"), ("now", "Tocando"), ("keys", "Atalhos"),
        ("mac", "Sistema (aba \"Mac\" na placa)"), ("docker", "Docker"), ("clock", "Hora"),
        ("audio", "Mixer"), ("apps", "Apps"), ("discord", "Discord (ainda não no Windows)"),
    ];

    public string HostId { get; set; } = Guid.NewGuid().ToString();
    /// How this PC is called in the board's host list.
    public string HostName { get; set; } = "PC";
    /// Board address for networks where mDNS does not get through. Empty: find waveshare.local.
    public string BoardAddress { get; set; } = "";
    /// Board id → token from pairing.
    public Dictionary<string, string> Tokens { get; set; } = new();

    public List<string> Pages { get; set; } = ["now", "keys", "mac", "clock", "audio", "apps", "docker"];

    public List<KeyItem> Keys { get; set; } =
    [
        new("Captura", "image", "builtin", "shot"),
        new("Microfone", "mic", "builtin", "mic"),
        new("Apagar tela", "eye_close", "builtin", "sleep"),
        new("Volume -", "volume_mid", "builtin", "voldown"),
        new("Volume +", "volume_max", "builtin", "volup"),
        new("Mudo", "mute", "builtin", "mute"),
    ];

    public List<KeyItem> Apps { get; set; } =
    [
        new("Terminal", "keyboard", "app", "wt.exe", "term"),
        new("Explorador", "directory", "app", "explorer.exe", "explorer"),
        new("Navegador", "wifi", "url", "https://www.google.com", "browser"),
        new("VS Code", "edit", "command", "code", "code"),
        new("Discord", "call", "url", "discord://", "discord"),
        new("Spotify", "audio", "url", "spotify:", "music"),
    ];

    // Weather on the Hora page (Open-Meteo).
    public string City { get; set; } = "Cachoeiro de Itapemirim";
    public double Latitude { get; set; } = -20.85;
    public double Longitude { get; set; } = -41.11;

    public static Config Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var c = JsonSerializer.Deserialize<Config>(File.ReadAllText(FilePath), Options);
                if (c != null)
                {
                    c.Tokens ??= new();
                    c.Pages ??= [];
                    c.Keys ??= [];
                    c.Apps ??= [];
                    if (string.IsNullOrWhiteSpace(c.HostId)) c.HostId = Guid.NewGuid().ToString();
                    c.Save(); // fills in settings added in a later version
                    return c;
                }
            }
        }
        catch (Exception e)
        {
            Log.Error("settings.json inválido, usando o padrão", e);
            try { File.Copy(FilePath, FilePath + ".invalido", overwrite: true); } catch { }
        }
        var d = new Config();
        d.Save();
        return d;
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Dir);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Options));
        }
        catch (Exception e)
        {
            Log.Error("salvar settings.json", e);
        }
    }

    public KeyItem? Key(string boardId) => Keys.FirstOrDefault(k => k.BoardId == boardId);
    public KeyItem? App(string boardId) => Apps.FirstOrDefault(k => k.BoardId == boardId);
    public bool IsEnabled(string page) => Pages.Contains(page);

    public JsonObject LayoutMessage()
    {
        static JsonObject Button(KeyItem k) => new()
        {
            ["id"] = k.BoardId,
            ["icon"] = k.Icon,
            ["text"] = Text.Ascii(k.Text),
        };
        var known = AllPages.Select(p => p.Id).ToHashSet();
        return new JsonObject
        {
            ["t"] = "layout",
            ["pages"] = Json.Array(Pages.Where(known.Contains).Select(p => (JsonNode?)JsonValue.Create(p))),
            ["keys"] = Json.Array(Keys.Take(12).Select(k => (JsonNode?)Button(k))),
            ["apps"] = Json.Array(Apps.Take(12).Select(k => (JsonNode?)Button(k))),
        };
    }
}

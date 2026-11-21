using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using WaveshareLink.Ui;

namespace WaveshareLink;

/// The connection to the board over Wi-Fi: TCP port 7270, one JSON object per line.
/// Same protocol as the Mac app (README, "Protocolo"):
///   board: {"t":"hello","v":2,"id","nonce"}
///   PC:    {"t":"auth","hostId","name","os","nonce","hmac"}   hmac = HMAC-SHA256(token, "host:"+board nonce)
///   board: {"t":"auth","ok","hmac"}                            hmac = HMAC-SHA256(token, "board:"+our nonce)
///   board: {"t":"active","on"}  only the active host's data reaches the screen
///   First time: {"t":"pair"} → the board shows a code → {"t":"pair","code"} → {"t":"paired","token"}
/// Everything runs on the UI thread.
sealed class BoardLink
{
    public const int Port = 7270;

    public event Action<string>? Press;
    public event Action<string>? PageChanged;
    public event Action? StatusChanged;
    /// Everything the board shows, sent whenever this PC goes (back) on the board's screen.
    public Func<IEnumerable<JsonObject>> Snapshot { get; set; } = () => [];

    public bool Linked { get; private set; }
    public bool Active { get; private set; }
    public bool IsLive => Linked && Active;
    public bool Connected => stream != null;
    public bool NeedsPairing { get; private set; }
    public string? Address { get; private set; }
    public string? Problem { get; private set; }
    /// The board's own Wi-Fi: state, ssid, ip, rssi, why.
    public JsonObject? BoardNet { get; private set; }

    readonly Config config;
    readonly SynchronizationContext ui;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 2000 };
    TcpClient? client;
    NetworkStream? stream;
    readonly Queue<byte[]> outbox = new();
    int outboxBytes;
    bool pumping;
    bool connecting;
    DateTime lastRx;
    DateTime nextAttempt = DateTime.MinValue;
    string? boardId;
    string boardNonce = "";
    string myNonce = "";
    bool prompted;
    bool askingCode;
    int generation; // bumped on every close, so loops of an old connection stop

    public BoardLink(Config config)
    {
        this.config = config;
        ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
        timer.Tick += (_, _) => Tick();
    }

    string HostName
    {
        get
        {
            var n = Text.Ascii(config.HostName.Trim());
            return Text.Cut(n.Length == 0 ? "PC" : n, 23);
        }
    }

    public void Start()
    {
        timer.Start();
        Tick();
    }

    /// The address in the settings changed.
    public void Reconnect()
    {
        Close();
        nextAttempt = DateTime.MinValue;
        Tick();
    }

    /// The name in the settings changed.
    public void Rename()
    {
        if (Linked) Send(new JsonObject { ["t"] = "name", ["name"] = HostName });
    }

    void Tick()
    {
        if (stream == null)
        {
            if (!connecting && DateTime.Now >= nextAttempt) _ = ConnectAsync();
            return;
        }
        if (Linked)
        {
            Send(new JsonObject { ["t"] = "ping" });
            // The board answers every ping; silence means it is gone.
            if ((DateTime.Now - lastRx).TotalSeconds > 8)
            {
                Log.Info("A placa parou de responder. Reconectando.");
                Close();
            }
        }
    }

    async Task ConnectAsync()
    {
        connecting = true;
        TcpClient? c = null;
        try
        {
            var ip = await Discovery.FindBoardAsync(config.BoardAddress);
            if (ip == null)
            {
                nextAttempt = DateTime.Now.AddSeconds(5);
                return;
            }
            c = new TcpClient(AddressFamily.InterNetwork) { NoDelay = true };
            using (var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5)))
                await c.ConnectAsync(ip, Port, cts.Token);
            client = c;
            c = null;
            stream = client.GetStream();
            Address = ip.ToString();
            lastRx = DateTime.Now;
            Log.Info($"Conectado à placa em {ip}:{Port}");
            var gen = ++generation;
            _ = ReadLoop(stream, gen);
            Changed();
        }
        catch (Exception e)
        {
            Log.Info($"Não conectou à placa: {e.Message}");
            nextAttempt = DateTime.Now.AddSeconds(3);
        }
        finally
        {
            c?.Dispose();
            connecting = false;
        }
    }

    async Task ReadLoop(NetworkStream s, int gen)
    {
        var buf = new byte[16384];
        var pending = new MemoryStream();
        try
        {
            while (gen == generation)
            {
                int n = await s.ReadAsync(buf);
                if (n <= 0) break;
                if (gen != generation) return;
                lastRx = DateTime.Now;
                int start = 0;
                for (int i = 0; i < n; i++)
                {
                    if (buf[i] != (byte)'\n') continue;
                    pending.Write(buf, start, i - start);
                    var line = Encoding.UTF8.GetString(pending.GetBuffer(), 0, (int)pending.Length);
                    pending.SetLength(0);
                    start = i + 1;
                    HandleLine(line);
                    if (gen != generation) return; // closed while handling
                }
                pending.Write(buf, start, n - start);
                if (pending.Length > 256 * 1024) pending.SetLength(0); // not our protocol
            }
        }
        catch (Exception e)
        {
            if (gen != generation) return;
            Log.Info($"A conexão caiu: {e.Message}");
        }
        if (gen == generation) Close();
    }

    public void Close()
    {
        generation++;
        try { stream?.Dispose(); } catch { }
        try { client?.Dispose(); } catch { }
        stream = null;
        client = null;
        outbox.Clear();
        outboxBytes = 0;
        Address = null;
        Linked = false;
        Active = false;
        nextAttempt = DateTime.Now.AddSeconds(3);
        Changed();
    }

    public void Send(JsonObject msg)
    {
        if (stream == null) return;
        var bytes = Encoding.UTF8.GetBytes(msg.ToJsonString() + "\n");
        if (outboxBytes + bytes.Length > 1024 * 1024) return; // the board stopped reading
        outbox.Enqueue(bytes);
        outboxBytes += bytes.Length;
        if (!pumping) _ = Pump();
    }

    async Task Pump()
    {
        pumping = true;
        var gen = generation;
        try
        {
            while (outbox.Count > 0 && stream != null && gen == generation)
            {
                var b = outbox.Dequeue();
                outboxBytes -= b.Length;
                await stream.WriteAsync(b);
            }
        }
        catch (Exception e)
        {
            if (gen == generation)
            {
                Log.Info($"Erro ao enviar: {e.Message}");
                Close();
            }
        }
        finally
        {
            pumping = false;
            // A new connection queued messages while the old write was failing.
            if (outbox.Count > 0 && stream != null && gen != generation) _ = Pump();
        }
    }

    void HandleLine(string line)
    {
        JsonObject? o;
        try { o = JsonNode.Parse(line) as JsonObject; }
        catch (JsonException) { return; }
        if (o == null) return;

        switch (Json.Str(o, "t"))
        {
            case "hello": OnHello(o); break;
            case "auth": OnAuth(o); break;
            case "pair": OnPairState(Json.Str(o, "state") ?? ""); break;
            case "paired": OnPaired(o); break;
            case "active": SetActive(Json.Bool(o, "on")); break;
            case "net":
                BoardNet = o;
                Changed();
                break;
            case "press":
                if (IsLive && Json.Str(o, "id") is string pressId) Press?.Invoke(pressId);
                break;
            case "page":
                if (IsLive && Json.Str(o, "id") is string pageId) PageChanged?.Invoke(pageId);
                break;
        }
    }

    // --- Handshake ---------------------------------------------------------------

    void OnHello(JsonObject o)
    {
        boardId = Json.Str(o, "id");
        boardNonce = Json.Str(o, "nonce") ?? "";
        if (boardId == null) return;
        if (config.Tokens.TryGetValue(boardId, out var token))
        {
            myNonce = RandomHex(8);
            Send(new JsonObject
            {
                ["t"] = "auth",
                ["hostId"] = config.HostId,
                ["name"] = HostName,
                ["os"] = "windows",
                ["nonce"] = myNonce,
                ["hmac"] = Hmac(token, "host:" + boardNonce),
            });
        }
        else
        {
            NeedPairing();
        }
    }

    void OnAuth(JsonObject o)
    {
        if (boardId == null) return;
        if (!Json.Bool(o, "ok"))
        {
            Log.Info("A placa não reconhece este PC (pareamentos apagados?). É preciso parear de novo.");
            config.Tokens.Remove(boardId);
            config.Save();
            NeedPairing();
            return;
        }
        if (!config.Tokens.TryGetValue(boardId, out var token) || Json.Str(o, "hmac") != Hmac(token, "board:" + myNonce))
        {
            // Something on the network pretends to be the board; its presses would run here.
            Problem = "A placa na rede não provou a identidade. Conexão recusada.";
            Log.Info(Problem);
            Close();
            return;
        }
        SetLinked();
    }

    void SetLinked()
    {
        Linked = true;
        NeedsPairing = false;
        Problem = null;
        Log.Info("Autenticado na placa.");
        Changed();
    }

    void SetActive(bool on)
    {
        if (!Linked) return;
        Active = on;
        Log.Info(on ? "Este PC está na tela da placa." : "Outro computador está na tela da placa.");
        // On screen again: the board needs our layout and every page's state.
        if (on)
        {
            foreach (var m in Snapshot()) Send(m);
        }
        Changed();
    }

    // --- Pairing -------------------------------------------------------------------

    void NeedPairing()
    {
        NeedsPairing = true;
        Changed();
        if (!prompted)
        {
            prompted = true;
            StartPairing();
        }
    }

    /// Asks the board to show a code (menu "Parear com a placa…" and the first connection).
    public void StartPairing()
    {
        if (stream == null || Linked)
        {
            Problem = "A placa não foi encontrada na rede.";
            Changed();
            return;
        }
        Problem = null;
        Send(new JsonObject { ["t"] = "pair", ["hostId"] = config.HostId, ["name"] = HostName, ["os"] = "windows" });
    }

    void OnPairState(string state)
    {
        switch (state)
        {
            case "code": AskCode("Digite o código de 6 dígitos que apareceu na tela da placa."); break;
            case "wrong": AskCode("Código errado. Confira na tela da placa e tente de novo."); break;
            case "busy": SetProblem("Outro computador está pareando com a placa agora."); break;
            case "failed": SetProblem("Código errado 3 vezes. Use \"Parear com a placa…\" no menu."); break;
            case "nocode": StartPairing(); break;
            // "cancelled": the board closes the connection.
        }
    }

    void SetProblem(string text)
    {
        Problem = text;
        Log.Info(text);
        Changed();
    }

    void AskCode(string message)
    {
        if (askingCode) return;
        askingCode = true;
        var gen = generation;
        // Out of the read loop before running a modal dialog.
        ui.Post(_ =>
        {
            var code = PairDialog.Ask(message);
            askingCode = false;
            if (gen != generation || Linked) return;
            if (code == null)
            {
                Close(); // the board hides the code when we leave
                return;
            }
            Send(new JsonObject
            {
                ["t"] = "pair",
                ["code"] = code,
                ["hostId"] = config.HostId,
                ["name"] = HostName,
                ["os"] = "windows",
            });
        }, null);
    }

    void OnPaired(JsonObject o)
    {
        var token = Json.Str(o, "token");
        var id = Json.Str(o, "id") ?? boardId;
        if (token == null || id == null) return;
        config.Tokens[id] = token;
        config.Save();
        Log.Info("Pareado com a placa.");
        SetLinked();
    }

    void Changed() => StatusChanged?.Invoke();

    // --- Crypto --------------------------------------------------------------------

    static string Hmac(string key, string message)
    {
        using var h = new HMACSHA256(Encoding.UTF8.GetBytes(key));
        return Convert.ToHexString(h.ComputeHash(Encoding.UTF8.GetBytes(message))).ToLowerInvariant();
    }

    static string RandomHex(int bytes) => Convert.ToHexString(RandomNumberGenerator.GetBytes(bytes)).ToLowerInvariant();
}

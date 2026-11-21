using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Text.Json.Nodes;
using Microsoft.Win32;
using WaveshareLink.Feeds;
using WaveshareLink.Ui;

namespace WaveshareLink;

/// The tray icon and the glue: board messages in, feeds out. Like the Mac app, data is only
/// gathered while this PC is on the board's screen, and per page only while that page is visible.
sealed class TrayApp : ApplicationContext
{
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    readonly Config config = Config.Load();
    readonly BoardLink board;
    readonly MediaFeed media = new();
    readonly AudioFeed audio;
    readonly SystemFeed system = new();
    readonly ClockFeed clock;
    readonly DockerFeed docker = new();
    readonly NotifyIcon tray = new();
    readonly ContextMenuStrip menu = new();
    readonly Icon iconLive = MakeIcon(Color.FromArgb(61, 190, 120));
    readonly Icon iconIdle = MakeIcon(Color.FromArgb(138, 143, 152));
    // Mic and camera state while live; the Mixer while its page is open.
    readonly System.Windows.Forms.Timer micTimer = new() { Interval = 2000 };
    readonly System.Windows.Forms.Timer audioTimer = new() { Interval = 500 };
    string lastMic = "";
    string lastAudio = "";
    string page = "";

    public TrayApp()
    {
        board = new BoardLink(config);
        audio = new AudioFeed(() => media.SourceAppId, () => media.AppName);
        clock = new ClockFeed(config);

        tray.Icon = iconIdle;
        tray.Text = "Waveshare Link";
        tray.ContextMenuStrip = menu;
        tray.Visible = true;
        menu.Opening += (_, e) =>
        {
            BuildMenu();
            e.Cancel = false;
        };

        media.OnChange = () =>
        {
            Push(media.Message);
            if (page == "audio") PushAudio(force: true); // the media slider follows the player
        };
        media.OnArt = () => Push(media.ArtMessage);
        system.OnSample = Push;
        clock.OnChange = () => Push(clock.Message);
        docker.OnChange = () => Push(docker.Message);
        micTimer.Tick += (_, _) => PushMic(force: false);
        audioTimer.Tick += (_, _) => PushAudio(force: false);

        board.Snapshot = Snapshot;
        board.Press += OnPress;
        board.PageChanged += OnPage;
        board.StatusChanged += OnStatus;

        _ = media.StartAsync();
        board.Start();
        Log.Info($"Ajustes em {Config.FilePath}");
    }

    /// Everything the board shows, layout first (it brings the tabs and keys).
    IEnumerable<JsonObject> Snapshot()
    {
        lastMic = "";
        lastAudio = "";
        var mic = audio.MicMessage();
        var mix = audio.Message();
        lastMic = mic.ToJsonString();
        lastAudio = mix.ToJsonString();
        return [config.LayoutMessage(), media.Message, media.ArtMessage, mic, clock.Message, mix];
    }

    void Push(JsonObject? msg)
    {
        if (board.IsLive && msg != null) board.Send(msg);
    }

    void PushMic(bool force)
    {
        if (!board.IsLive) return;
        var m = audio.MicMessage();
        var json = m.ToJsonString();
        if (!force && json == lastMic) return;
        lastMic = json;
        board.Send(m);
    }

    void PushAudio(bool force)
    {
        if (!board.IsLive) return;
        var m = audio.Message();
        var json = m.ToJsonString();
        if (!force && json == lastAudio) return;
        lastAudio = json;
        board.Send(m);
    }

    void OnPress(string id)
    {
        Log.Info($"Toque: {id}");
        if (id.StartsWith("key:")) PressKey(id[4..]);
        else if (id.StartsWith("app:")) { if (config.App(id[4..]) is KeyItem app) KeyRunner.Run(app); }
        else if (id.StartsWith("vol:")) { audio.HandleVolume(id[4..]); PushAudio(force: false); }
        else if (id.StartsWith("mute:")) { audio.HandleMute(id[5..]); PushAudio(force: false); PushMic(force: false); }
        else if (id.StartsWith("outdev:")) { audio.HandleOutputDevice(id[7..]); PushAudio(force: false); }
        // "refresh" (Uso) and "discord:*" are not on Windows yet.
    }

    /// Atalhos keys (from the settings) and the media buttons of Tocando and the music dock.
    async void PressKey(string id)
    {
        var key = config.Key(id);
        if (id == "mic" || (key?.Kind == "builtin" && key.Value == "mic"))
        {
            audio.ToggleMicMute();
            PushMic(force: true);
            return;
        }
        if (key != null && key.Kind != "builtin")
        {
            KeyRunner.Run(key);
            return;
        }
        var action = key?.Value ?? id;
        // Play/next/prev go straight to the player when there is one (more reliable than media keys).
        if (action is "play" or "next" or "prev" && await media.ControlAsync(action)) return;
        KeyRunner.Builtin(action);
    }

    /// Only the visible page's data is gathered.
    void OnPage(string id)
    {
        page = id;
        system.SetActive(id == "mac");
        docker.SetActive(id == "docker");
        clock.SetActive(id == "clock");
        media.SetPageActive(id == "now");
        audioTimer.Enabled = id == "audio";
        if (id == "audio") PushAudio(force: true);
    }

    void OnStatus()
    {
        bool live = board.IsLive;
        tray.Icon = live ? iconLive : iconIdle;
        var status = StatusLine();
        tray.Text = Text.Cut("Waveshare Link: " + status, 127);
        micTimer.Enabled = live;
        if (!live) OnPage("");
    }

    string StatusLine()
    {
        if (board.IsLive) return $"na tela ({board.Address})";
        if (board.Linked) return "conectado, outro computador na tela";
        if (board.NeedsPairing) return "precisa parear";
        if (board.Connected) return $"conectando ({board.Address})";
        return "procurando a placa na rede";
    }

    // --- Menu ------------------------------------------------------------------------

    void BuildMenu()
    {
        menu.Items.Clear();
        Disabled("Placa: " + StatusLine());
        if (board.Linked && !board.Active) Disabled("Toque no nome no topo da placa para trocar");
        if (BoardNetLine() is string net) Disabled(net);
        if (board.Problem is string problem) Disabled("⚠ " + problem);
        if (board.NeedsPairing && !board.Linked) Item("Parear com a placa…", board.StartPairing);
        menu.Items.Add(new ToolStripSeparator());
        Item("Ajustes…", OpenSettings);
        Item("Recarregar ajustes", ReloadSettings);
        Item("Abrir pasta de ajustes e log", () => Process.Start(new ProcessStartInfo("explorer.exe", $"\"{Config.Dir}\"") { UseShellExecute = false }));
        var autostart = new ToolStripMenuItem("Iniciar com o Windows") { Checked = AutostartEnabled };
        autostart.Click += (_, _) => SetAutostart(!AutostartEnabled);
        menu.Items.Add(autostart);
        menu.Items.Add(new ToolStripSeparator());
        Item("Sair", Quit);
    }

    void Disabled(string text) => menu.Items.Add(new ToolStripMenuItem(text) { Enabled = false });

    void Item(string text, Action action)
    {
        var i = new ToolStripMenuItem(text);
        i.Click += (_, _) =>
        {
            try { action(); }
            catch (Exception e) { Log.Error(text, e); }
        };
        menu.Items.Add(i);
    }

    string? BoardNetLine()
    {
        var n = board.BoardNet;
        if (n == null) return null;
        var ssid = Json.Str(n, "ssid") ?? "";
        return Json.Str(n, "state") switch
        {
            "connected" => $"Wi-Fi da placa: {ssid} ({Json.Str(n, "ip")}, {n["rssi"]} dBm)",
            "connecting" => $"Wi-Fi da placa: conectando a {ssid}…",
            _ => null,
        };
    }

    void OpenSettings()
    {
        var oldAddress = config.BoardAddress;
        var oldCity = config.City;
        using var f = new SettingsForm(config, () =>
        {
            board.Rename();
            if (config.City != oldCity) clock.Invalidate();
            Push(config.LayoutMessage());
            if (config.BoardAddress != oldAddress) board.Reconnect();
        });
        f.ShowDialog();
    }

    /// After editing settings.json by hand.
    void ReloadSettings()
    {
        var fresh = Config.Load();
        // Keep the identity and tokens of this run; take everything the user edits.
        config.HostName = fresh.HostName;
        config.BoardAddress = fresh.BoardAddress;
        config.Pages = fresh.Pages;
        config.Keys = fresh.Keys;
        config.Apps = fresh.Apps;
        config.City = fresh.City;
        config.Latitude = fresh.Latitude;
        config.Longitude = fresh.Longitude;
        config.Save();
        Log.Info("Ajustes recarregados.");
        board.Rename();
        clock.Invalidate();
        Push(config.LayoutMessage());
    }

    static bool AutostartEnabled
    {
        get
        {
            using var k = Registry.CurrentUser.OpenSubKey(RunKey);
            return k?.GetValue("WaveshareLink") != null;
        }
    }

    static void SetAutostart(bool on)
    {
        using var k = Registry.CurrentUser.CreateSubKey(RunKey);
        if (on && Environment.ProcessPath is string exe) k.SetValue("WaveshareLink", $"\"{exe}\"");
        else k.DeleteValue("WaveshareLink", throwOnMissingValue: false);
    }

    void Quit()
    {
        tray.Visible = false;
        board.Close();
        ExitThread();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            tray.Dispose();
            menu.Dispose();
        }
        base.Dispose(disposing);
    }

    /// A small rounded "screen" in the given color (green: on the board's screen).
    static Icon MakeIcon(Color color)
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using var path = new GraphicsPath();
            var r = new Rectangle(2, 5, 28, 20);
            int d = 8;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            using var brush = new SolidBrush(color);
            g.FillPath(brush, path);
            using var stand = new SolidBrush(color);
            g.FillRectangle(stand, 12, 25, 8, 4);
        }
        return Icon.FromHandle(bmp.GetHicon());
    }
}

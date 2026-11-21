using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Text.Json.Nodes;
using Windows.Media.Control;
using Windows.Storage.Streams;

namespace WaveshareLink.Feeds;

/// "Tocando": whatever Windows shows in its media overlay (Spotify, browsers, Media Player…),
/// through the system media session API. Works with any player that supports it, cover included.
///   {"t":"now","app","title","artist","album","playing","pos","dur"}   app "" = nothing playing
///   {"t":"art","jpg":"<base64 220x220 JPEG>"}                          no jpg = no cover
sealed class MediaFeed
{
    const int ArtSide = 220;

    public Action? OnChange;
    public Action? OnArt;
    public JsonObject Message { get; private set; } = new() { ["t"] = "now", ["app"] = "" };
    public JsonObject ArtMessage { get; private set; } = new() { ["t"] = "art" };
    /// The player's AppUserModelId (e.g. "Spotify.exe"), used by the Mixer to find its volume.
    public string SourceAppId { get; private set; } = "";
    public string AppName { get; private set; } = "";

    readonly SynchronizationContext ui;
    readonly System.Windows.Forms.Timer progress = new() { Interval = 2000 };
    GlobalSystemMediaTransportControlsSessionManager? manager;
    GlobalSystemMediaTransportControlsSession? session;
    string trackKey = "";
    bool artHasImage;
    bool refreshing;
    bool again;

    public MediaFeed()
    {
        ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
        progress.Tick += (_, _) => Kick();
    }

    public async Task StartAsync()
    {
        try
        {
            manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            manager.CurrentSessionChanged += (_, _) => ui.Post(_ => Attach(), null);
            manager.SessionsChanged += (_, _) => ui.Post(_ => Attach(), null);
            Attach();
        }
        catch (Exception e)
        {
            Log.Error("mídia (sessões do Windows)", e);
        }
    }

    /// The position only moves on screen while the Tocando page is open.
    public void SetPageActive(bool on) => progress.Enabled = on;

    void Attach()
    {
        if (manager == null) return;
        if (session != null)
        {
            session.MediaPropertiesChanged -= OnMediaProperties;
            session.PlaybackInfoChanged -= OnPlaybackInfo;
            session.TimelinePropertiesChanged -= OnTimeline;
        }
        session = manager.GetCurrentSession();
        if (session != null)
        {
            session.MediaPropertiesChanged += OnMediaProperties;
            session.PlaybackInfoChanged += OnPlaybackInfo;
            session.TimelinePropertiesChanged += OnTimeline;
        }
        Kick();
    }

    // Raised on background threads.
    void OnMediaProperties(GlobalSystemMediaTransportControlsSession s, MediaPropertiesChangedEventArgs e) => ui.Post(_ => Kick(), null);
    void OnPlaybackInfo(GlobalSystemMediaTransportControlsSession s, PlaybackInfoChangedEventArgs e) => ui.Post(_ => Kick(), null);
    void OnTimeline(GlobalSystemMediaTransportControlsSession s, TimelinePropertiesChangedEventArgs e) => ui.Post(_ => Kick(), null);

    void Kick() => _ = RefreshAsync();

    async Task RefreshAsync()
    {
        if (refreshing)
        {
            again = true;
            return;
        }
        refreshing = true;
        try
        {
            do
            {
                again = false;
                await RefreshOnce();
            } while (again);
        }
        catch (Exception e)
        {
            Log.Error("mídia", e);
        }
        finally
        {
            refreshing = false;
        }
    }

    async Task RefreshOnce()
    {
        var s = session;
        if (s == null)
        {
            Clear();
            return;
        }
        var props = await s.TryGetMediaPropertiesAsync();
        if (props == null || string.IsNullOrEmpty(props.Title))
        {
            Clear();
            return;
        }
        var playback = s.GetPlaybackInfo();
        var timeline = s.GetTimelineProperties();
        bool playing = playback?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;

        double pos = 0, dur = 0;
        if (timeline != null)
        {
            dur = Math.Max(0, (timeline.EndTime - timeline.StartTime).TotalSeconds);
            var p = timeline.Position - timeline.StartTime;
            // The position is only updated now and then; move it forward ourselves while playing.
            if (playing && timeline.LastUpdatedTime.Year > 2000) p += DateTimeOffset.Now - timeline.LastUpdatedTime;
            pos = Math.Max(0, p.TotalSeconds);
            if (dur > 0) pos = Math.Min(pos, dur);
        }

        SourceAppId = s.SourceAppUserModelId ?? "";
        AppName = FriendlyApp(SourceAppId);
        Message = new JsonObject
        {
            ["t"] = "now",
            ["app"] = Text.Ascii(AppName),
            ["title"] = Text.Ascii(props.Title),
            ["artist"] = Text.Ascii(props.Artist),
            ["album"] = Text.Ascii(props.AlbumTitle),
            ["playing"] = playing,
            ["pos"] = Math.Round(pos, 1),
            ["dur"] = Math.Round(dur, 1),
        };
        OnChange?.Invoke();

        // The cover often arrives a moment after the title: try again until there is one.
        var key = $"{SourceAppId}|{props.Title}|{props.Artist}|{props.AlbumTitle}";
        if (key != trackKey || (!artHasImage && props.Thumbnail != null))
        {
            trackKey = key;
            var jpg = await CoverAsync(props.Thumbnail);
            if (key != trackKey) return; // the track changed while loading
            artHasImage = jpg != null;
            ArtMessage = jpg != null ? new JsonObject { ["t"] = "art", ["jpg"] = jpg } : new JsonObject { ["t"] = "art" };
            OnArt?.Invoke();
        }
    }

    void Clear()
    {
        bool hadSomething = Json.Str(Message, "app") is { Length: > 0 };
        SourceAppId = "";
        AppName = "";
        Message = new JsonObject { ["t"] = "now", ["app"] = "" };
        if (hadSomething) OnChange?.Invoke();
        if (trackKey != "")
        {
            trackKey = "";
            artHasImage = false;
            ArtMessage = new JsonObject { ["t"] = "art" };
            OnArt?.Invoke();
        }
    }

    /// The board's Prev / Play / Next (Tocando page and the music dock).
    public async Task<bool> ControlAsync(string action)
    {
        var s = session;
        if (s == null) return false;
        try
        {
            return action switch
            {
                "play" => await s.TryTogglePlayPauseAsync(),
                "next" => await s.TrySkipNextAsync(),
                "prev" => await s.TrySkipPreviousAsync(),
                _ => false,
            };
        }
        catch (Exception e)
        {
            Log.Error("controle de mídia", e);
            return false;
        }
    }

    static string FriendlyApp(string id)
    {
        var l = id.ToLowerInvariant();
        if (l.Contains("spotify")) return "Spotify";
        if (l.Contains("zunemusic") || l.Contains("mediaplayer")) return "Media Player";
        if (l.Contains("chrome")) return "Chrome";
        if (l.Contains("msedge")) return "Edge";
        if (l.Contains("firefox")) return "Firefox";
        if (l.Contains("vlc")) return "VLC";
        if (l.Contains("deezer")) return "Deezer";
        if (l.Contains("tidal")) return "Tidal";
        if (l.Contains("applemusic") || l.Contains("itunes")) return "Apple Music";
        var name = id.Split('!')[0];
        if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) name = name[..^4];
        var dot = name.LastIndexOf('.');
        if (dot >= 0 && dot < name.Length - 1) name = name[(dot + 1)..];
        var underscore = name.IndexOf('_');
        if (underscore > 0) name = name[..underscore];
        return name.Length > 0 ? Text.Cut(name, 20) : "Midia";
    }

    static async Task<string?> CoverAsync(IRandomAccessStreamReference? thumb)
    {
        if (thumb == null) return null;
        try
        {
            using var ras = await thumb.OpenReadAsync();
            var size = (uint)ras.Size;
            if (size == 0) return null;
            using var input = ras.GetInputStreamAt(0);
            using var reader = new DataReader(input);
            await reader.LoadAsync(size);
            var bytes = new byte[size];
            reader.ReadBytes(bytes);
            return Encode(bytes);
        }
        catch (Exception e)
        {
            Log.Error("capa", e);
            return null;
        }
    }

    /// 220x220 baseline JPEG, cropped to fill (the board decodes it with tjpgd).
    static string Encode(byte[] data)
    {
        using var ms = new MemoryStream(data);
        using var src = Image.FromStream(ms);
        using var bmp = new Bitmap(ArtSide, ArtSide, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bmp))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            double scale = Math.Max((double)ArtSide / src.Width, (double)ArtSide / src.Height);
            int w = (int)Math.Ceiling(src.Width * scale), h = (int)Math.Ceiling(src.Height * scale);
            g.DrawImage(src, (ArtSide - w) / 2, (ArtSide - h) / 2, w, h);
        }
        var codec = ImageCodecInfo.GetImageEncoders().First(c => c.FormatID == ImageFormat.Jpeg.Guid);
        using var ep = new EncoderParameters(1);
        ep.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, 60L);
        using var output = new MemoryStream();
        bmp.Save(output, codec, ep);
        return Convert.ToBase64String(output.ToArray());
    }
}

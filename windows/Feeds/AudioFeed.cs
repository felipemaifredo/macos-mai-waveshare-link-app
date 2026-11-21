using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using NAudio.CoreAudioApi;

namespace WaveshareLink.Feeds;

/// "Mixer" and the microphone, through Windows Core Audio.
///   {"t":"audio","master":{"vol","muted"},"media":{"app","vol","muted"},"mic":{"vol","muted"},
///    "activeDev","devices":[{"id","name","active"}]}
///   {"t":"mic","muted","micInUse","camInUse"}
/// Board → PC: vol:master|media|mic:<0..100>, mute:master|media|mic, outdev:<id>.
/// "media" is the volume of the player shown on Tocando (its audio session).
/// The board keeps device ids in 16 characters; Windows ids are ~55, so they get short ones ("d1", "d2"…).
sealed class AudioFeed
{
    const int MaxDevices = 6; // buttons on the board

    readonly MMDeviceEnumerator enumerator = new();
    readonly Func<string> mediaAppId;
    readonly Func<string> mediaAppName;
    readonly Dictionary<string, string> shortIds = new();
    readonly Dictionary<string, string> fullIds = new();

    public AudioFeed(Func<string> mediaAppId, Func<string> mediaAppName)
    {
        this.mediaAppId = mediaAppId;
        this.mediaAppName = mediaAppName;
    }

    MMDevice Output() => enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
    MMDevice Mic() => enumerator.GetDefaultAudioEndpoint(DataFlow.Capture, Role.Console);

    static int Pct(float scalar) => (int)Math.Round(Math.Clamp(scalar, 0f, 1f) * 100);

    public JsonObject Message()
    {
        var m = new JsonObject { ["t"] = "audio" };
        try
        {
            var dev = Output();
            var vol = dev.AudioEndpointVolume;
            m["master"] = new JsonObject { ["vol"] = Pct(vol.MasterVolumeLevelScalar), ["muted"] = vol.Mute };

            var media = MediaSession(dev);
            m["media"] = new JsonObject
            {
                ["app"] = Text.Ascii(media != null ? mediaAppName() : ""),
                ["vol"] = media != null ? Pct(media.SimpleAudioVolume.Volume) : 0,
                ["muted"] = media?.SimpleAudioVolume.Mute ?? false,
            };

            var devices = new JsonArray();
            foreach (var d in enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active).Take(MaxDevices))
            {
                devices.Add(new JsonObject
                {
                    ["id"] = ShortId(d.ID),
                    ["name"] = Text.Cut(Text.Ascii(d.FriendlyName), 32),
                    ["active"] = d.ID == dev.ID,
                });
            }
            m["activeDev"] = Text.Cut(Text.Ascii(dev.FriendlyName), 32);
            m["devices"] = devices;
        }
        catch (Exception e)
        {
            // No output device at all, or the audio service restarting.
            Log.Error("áudio", e);
            m["master"] = new JsonObject { ["vol"] = 0, ["muted"] = true };
            m["media"] = new JsonObject { ["app"] = "", ["vol"] = 0, ["muted"] = false };
            m["activeDev"] = "";
            m["devices"] = new JsonArray();
        }
        m["mic"] = MicVolume();
        return m;
    }

    JsonObject MicVolume()
    {
        try
        {
            var v = Mic().AudioEndpointVolume;
            return new JsonObject { ["vol"] = Pct(v.MasterVolumeLevelScalar), ["muted"] = v.Mute };
        }
        catch
        {
            return new JsonObject { ["vol"] = 0, ["muted"] = true }; // no microphone
        }
    }

    public bool MicMuted
    {
        get
        {
            try { return Mic().AudioEndpointVolume.Mute; }
            catch { return false; }
        }
    }

    public JsonObject MicMessage() => new()
    {
        ["t"] = "mic",
        ["muted"] = MicMuted,
        ["micInUse"] = Privacy.InUse("microphone"),
        ["camInUse"] = Privacy.InUse("webcam"),
    };

    public void ToggleMicMute()
    {
        try
        {
            var v = Mic().AudioEndpointVolume;
            v.Mute = !v.Mute;
        }
        catch (Exception e)
        {
            Log.Error("microfone", e);
        }
    }

    /// "master:75", "media:40", "mic:80"
    public void HandleVolume(string payload)
    {
        var parts = payload.Split(':');
        if (parts.Length != 2 || !int.TryParse(parts[1], out var pct)) return;
        float v = Math.Clamp(pct, 0, 100) / 100f;
        try
        {
            switch (parts[0])
            {
                case "master":
                    Output().AudioEndpointVolume.MasterVolumeLevelScalar = v;
                    break;
                case "media":
                    var session = MediaSession(Output());
                    if (session != null) session.SimpleAudioVolume.Volume = v;
                    break;
                case "mic":
                    Mic().AudioEndpointVolume.MasterVolumeLevelScalar = v;
                    break;
            }
        }
        catch (Exception e)
        {
            Log.Error("volume", e);
        }
    }

    public void HandleMute(string target)
    {
        try
        {
            switch (target)
            {
                case "master":
                    var vol = Output().AudioEndpointVolume;
                    vol.Mute = !vol.Mute;
                    break;
                case "media":
                    var session = MediaSession(Output());
                    if (session != null) session.SimpleAudioVolume.Mute = !session.SimpleAudioVolume.Mute;
                    break;
                case "mic":
                    ToggleMicMute();
                    break;
            }
        }
        catch (Exception e)
        {
            Log.Error("mudo", e);
        }
    }

    public void HandleOutputDevice(string shortId)
    {
        if (!fullIds.TryGetValue(shortId, out var id)) return;
        try
        {
            PolicyConfig.SetDefaultOutput(id);
        }
        catch (Exception e)
        {
            Log.Error("trocar saída de áudio", e);
        }
    }

    string ShortId(string fullId)
    {
        if (!shortIds.TryGetValue(fullId, out var s))
        {
            s = $"d{shortIds.Count + 1}";
            shortIds[fullId] = s;
            fullIds[s] = fullId;
        }
        return s;
    }

    /// The audio session of the player shown on Tocando: the process whose name appears in the
    /// player's AppUserModelId (e.g. "spotify" in "Spotify.exe" or "SpotifyAB.SpotifyMusic_…!Spotify").
    AudioSessionControl? MediaSession(MMDevice dev)
    {
        var app = mediaAppId().ToLowerInvariant();
        if (app.Length == 0) return null;
        var manager = dev.AudioSessionManager;
        manager.RefreshSessions();
        var sessions = manager.Sessions;
        for (int i = 0; i < sessions.Count; i++)
        {
            var s = sessions[i];
            if (s.IsSystemSoundsSession) continue;
            string name;
            try { name = Process.GetProcessById((int)s.GetProcessID).ProcessName.ToLowerInvariant(); }
            catch { continue; }
            if (name.Length > 0 && app.Contains(name)) return s;
        }
        return null;
    }
}

/// Changes the default output device. Windows has no public API for it; this undocumented COM
/// interface is what the Sound settings use (and every audio switcher app).
static class PolicyConfig
{
    [ComImport, Guid("f8679f50-850a-41cf-9c72-430f290290c8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPolicyConfig
    {
        [PreserveSig] int GetMixFormat([MarshalAs(UnmanagedType.LPWStr)] string device, IntPtr format);
        [PreserveSig] int GetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string device, int useDefault, IntPtr format);
        [PreserveSig] int ResetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string device);
        [PreserveSig] int SetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string device, IntPtr endpointFormat, IntPtr mixFormat);
        [PreserveSig] int GetProcessingPeriod([MarshalAs(UnmanagedType.LPWStr)] string device, int useDefault, IntPtr defaultPeriod, IntPtr minimumPeriod);
        [PreserveSig] int SetProcessingPeriod([MarshalAs(UnmanagedType.LPWStr)] string device, IntPtr period);
        [PreserveSig] int GetShareMode([MarshalAs(UnmanagedType.LPWStr)] string device, IntPtr mode);
        [PreserveSig] int SetShareMode([MarshalAs(UnmanagedType.LPWStr)] string device, IntPtr mode);
        [PreserveSig] int GetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string device, int fxStore, IntPtr key, IntPtr value);
        [PreserveSig] int SetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string device, int fxStore, IntPtr key, IntPtr value);
        [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string device, int role);
        [PreserveSig] int SetEndpointVisibility([MarshalAs(UnmanagedType.LPWStr)] string device, int visible);
    }

    [ComImport, Guid("870af99c-171d-4f9e-af0d-e63df40c2bc9")]
    class PolicyConfigClient { }

    public static void SetDefaultOutput(string deviceId)
    {
        var policy = (IPolicyConfig)new PolicyConfigClient();
        try
        {
            // eConsole, eMultimedia, eCommunications: the same device for everything.
            for (int role = 0; role < 3; role++)
            {
                int hr = policy.SetDefaultEndpoint(deviceId, role);
                if (hr != 0) Marshal.ThrowExceptionForHR(hr);
            }
        }
        finally
        {
            Marshal.ReleaseComObject(policy);
        }
    }
}

/// Microphone / camera in use, as Windows records it for the privacy indicators:
/// an app is using it while its LastUsedTimeStop is 0.
static class Privacy
{
    const string Root = @"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\";

    public static bool InUse(string capability)
    {
        try
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(Root + capability);
            return key != null && AnyInUse(key);
        }
        catch
        {
            return false;
        }
    }

    static bool AnyInUse(Microsoft.Win32.RegistryKey key)
    {
        foreach (var name in key.GetSubKeyNames())
        {
            using var sub = key.OpenSubKey(name);
            if (sub == null) continue;
            if (name == "NonPackaged")
            {
                if (AnyInUse(sub)) return true;
                continue;
            }
            if (sub.GetValue("LastUsedTimeStop") is long stop && stop == 0
                && sub.GetValue("LastUsedTimeStart") is long start && start > 0)
                return true;
        }
        return false;
    }
}

using System.Diagnostics;
using System.Runtime.InteropServices;

namespace WaveshareLink;

/// Runs the buttons of the Atalhos and Apps pages (see KeyItem for the kinds).
static class KeyRunner
{
    // Virtual-key codes of the media keys.
    static readonly Dictionary<string, byte> MediaKeys = new()
    {
        ["volup"] = 0xAF, ["voldown"] = 0xAE, ["mute"] = 0xAD,
        ["play"] = 0xB3, ["next"] = 0xB0, ["prev"] = 0xB1,
    };

    public static void Run(KeyItem k)
    {
        try
        {
            switch (k.Kind)
            {
                case "builtin":
                    Builtin(k.Value);
                    break;
                case "app":
                case "url":
                    Open(k.Value);
                    break;
                case "command":
                    if (string.IsNullOrWhiteSpace(k.Value)) return;
                    Process.Start(new ProcessStartInfo("cmd.exe", "/c " + k.Value) { UseShellExecute = false, CreateNoWindow = true });
                    break;
                case "powershell":
                    if (string.IsNullOrWhiteSpace(k.Value)) return;
                    var psi = new ProcessStartInfo("powershell.exe") { UseShellExecute = false, CreateNoWindow = true };
                    foreach (var a in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", k.Value }) psi.ArgumentList.Add(a);
                    Process.Start(psi);
                    break;
                default:
                    Log.Info($"Tipo de atalho desconhecido: {k.Kind}");
                    break;
            }
        }
        catch (Exception e)
        {
            Log.Error($"atalho \"{k.Text}\"", e);
        }
    }

    /// shot, sleep, lock and the media keys. The mic is handled by the caller (it needs the mixer).
    public static void Builtin(string id)
    {
        if (MediaKeys.TryGetValue(id, out var vk))
        {
            keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY, UIntPtr.Zero);
            keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, UIntPtr.Zero);
            return;
        }
        switch (id)
        {
            case "shot":
                Open("ms-screenclip:"); // the same as Win+Shift+S
                break;
            case "sleep":
                // Turns the monitors off (they come back with the mouse or keyboard).
                PostMessage(new IntPtr(HWND_BROADCAST), WM_SYSCOMMAND, new IntPtr(SC_MONITORPOWER), new IntPtr(2));
                break;
            case "lock":
                LockWorkStation();
                break;
            default:
                Log.Info($"Ação pronta desconhecida: {id}");
                break;
        }
    }

    static void Open(string target)
    {
        if (string.IsNullOrWhiteSpace(target)) return;
        Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
    }

    const uint KEYEVENTF_EXTENDEDKEY = 0x1;
    const uint KEYEVENTF_KEYUP = 0x2;
    const int HWND_BROADCAST = 0xFFFF;
    const uint WM_SYSCOMMAND = 0x0112;
    const int SC_MONITORPOWER = 0xF170;

    [DllImport("user32.dll")]
    static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern bool LockWorkStation();
}

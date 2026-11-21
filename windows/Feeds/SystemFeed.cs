using System.Net.NetworkInformation;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

namespace WaveshareLink.Feeds;

/// The "Mac" page (CPU, memory, battery, network), every 2 s while it is on screen.
///   {"t":"mac","cpu","mem","memUsed","memTotal","bat","charging","ac","rx","tx"}
sealed class SystemFeed
{
    public Action<JsonObject>? OnSample;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 2000 };
    ulong lastIdle, lastTotal;
    long lastRx, lastTx;
    DateTime lastNet;

    public SystemFeed() => timer.Tick += (_, _) => Sample();

    public void SetActive(bool on)
    {
        if (on && !timer.Enabled)
        {
            timer.Start();
            Sample();
        }
        else if (!on)
        {
            timer.Stop();
        }
    }

    void Sample()
    {
        var m = new JsonObject { ["t"] = "mac" };
        try
        {
            var cpu = Cpu();
            if (cpu >= 0) m["cpu"] = cpu;

            var mem = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() };
            if (GlobalMemoryStatusEx(ref mem) && mem.ullTotalPhys > 0)
            {
                double total = mem.ullTotalPhys / 1073741824.0;
                double used = (mem.ullTotalPhys - mem.ullAvailPhys) / 1073741824.0;
                m["memUsed"] = Math.Round(used, 1);
                m["memTotal"] = Math.Round(total);
                m["mem"] = (int)Math.Round(used / total * 100);
            }

            var p = SystemInformation.PowerStatus;
            bool noBattery = p.BatteryChargeStatus.HasFlag(BatteryChargeStatus.NoSystemBattery)
                             || p.BatteryChargeStatus == BatteryChargeStatus.Unknown;
            m["bat"] = noBattery ? -1 : (int)Math.Round(p.BatteryLifePercent * 100);
            m["charging"] = !noBattery && p.BatteryChargeStatus.HasFlag(BatteryChargeStatus.Charging);
            m["ac"] = p.PowerLineStatus == PowerLineStatus.Online;

            var (rx, tx) = Network();
            m["rx"] = rx;
            m["tx"] = tx;
        }
        catch (Exception e)
        {
            Log.Error("sistema", e);
        }
        OnSample?.Invoke(m);
    }

    int Cpu()
    {
        if (!GetSystemTimes(out long idle, out long kernel, out long user)) return -1;
        ulong total = (ulong)(kernel + user); // kernel time includes idle time
        bool first = lastTotal == 0;
        ulong dIdle = (ulong)idle - lastIdle, dTotal = total - lastTotal;
        lastIdle = (ulong)idle;
        lastTotal = total;
        if (first || dTotal == 0) return -1;
        return (int)Math.Round(Math.Clamp(1.0 - (double)dIdle / dTotal, 0, 1) * 100);
    }

    (long rx, long tx) Network()
    {
        long rx = 0, tx = 0;
        foreach (var n in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (n.OperationalStatus != OperationalStatus.Up) continue;
            if (n.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) continue;
            try
            {
                var s = n.GetIPStatistics();
                rx += s.BytesReceived;
                tx += s.BytesSent;
            }
            catch { }
        }
        var now = DateTime.Now;
        double secs = (now - lastNet).TotalSeconds;
        long r = 0, t = 0;
        if (lastNet != default && secs > 0)
        {
            r = (long)((rx - lastRx) / secs);
            t = (long)((tx - lastTx) / secs);
        }
        lastRx = rx;
        lastTx = tx;
        lastNet = now;
        return (Math.Max(0, r), Math.Max(0, t));
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MEMORYSTATUSEX
    {
        public uint dwLength;
        public uint dwMemoryLoad;
        public ulong ullTotalPhys;
        public ulong ullAvailPhys;
        public ulong ullTotalPageFile;
        public ulong ullAvailPageFile;
        public ulong ullTotalVirtual;
        public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX buffer);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetSystemTimes(out long idle, out long kernel, out long user);
}

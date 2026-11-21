using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;

namespace WaveshareLink;

/// Finds the board on the network. It announces itself over mDNS as waveshare.local
/// (service _wslink._tcp, port 7270). We ask for its address ourselves (a one-shot mDNS query on
/// every network adapter) and fall back to Windows' own name resolution.
static class Discovery
{
    public const string HostName = "waveshare.local";

    public static async Task<IPAddress?> FindBoardAsync(string manual)
    {
        manual = manual.Trim();
        if (manual.Length > 0)
        {
            if (IPAddress.TryParse(manual, out var ip)) return ip;
            return await ResolveAsync(manual);
        }
        return await QueryMdnsAsync(HostName) ?? await ResolveAsync(HostName);
    }

    static async Task<IPAddress?> ResolveAsync(string host)
    {
        try
        {
            using var cts = new CancellationTokenSource(3000);
            var list = await Dns.GetHostAddressesAsync(host, AddressFamily.InterNetwork, cts.Token);
            return list.FirstOrDefault();
        }
        catch (Exception e)
        {
            Log.Info($"Não achei {host}: {e.Message}");
            return null;
        }
    }

    static async Task<IPAddress?> QueryMdnsAsync(string name)
    {
        var query = BuildQuery(name);
        // With several adapters (VPN, Hyper-V, WSL), the default one may not be the right one.
        var tasks = LocalIPv4().Select(local => QueryOn(local, query, name)).ToList();
        if (tasks.Count == 0) tasks.Add(QueryOn(IPAddress.Any, query, name));
        while (tasks.Count > 0)
        {
            var done = await Task.WhenAny(tasks);
            tasks.Remove(done);
            var ip = await done;
            if (ip != null) return ip;
        }
        return null;
    }

    static IEnumerable<IPAddress> LocalIPv4()
    {
        try
        {
            return NetworkInterface.GetAllNetworkInterfaces()
                .Where(n => n.OperationalStatus == OperationalStatus.Up
                            && n.NetworkInterfaceType != NetworkInterfaceType.Loopback
                            && n.SupportsMulticast)
                .SelectMany(n => n.GetIPProperties().UnicastAddresses)
                .Where(a => a.Address.AddressFamily == AddressFamily.InterNetwork)
                .Select(a => a.Address)
                .ToList();
        }
        catch
        {
            return [];
        }
    }

    /// A "legacy unicast" query: sent from a random port, so the board answers straight to it.
    static async Task<IPAddress?> QueryOn(IPAddress local, byte[] query, string name)
    {
        try
        {
            using var udp = new UdpClient(new IPEndPoint(local, 0));
            if (!local.Equals(IPAddress.Any))
                udp.Client.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.MulticastInterface, local.GetAddressBytes());
            await udp.SendAsync(query, query.Length, new IPEndPoint(IPAddress.Parse("224.0.0.251"), 5353));
            using var cts = new CancellationTokenSource(1500);
            while (true)
            {
                var r = await udp.ReceiveAsync(cts.Token);
                var ip = ParseA(r.Buffer, name);
                if (ip != null) return ip;
            }
        }
        catch
        {
            return null; // timeout or adapter without multicast
        }
    }

    static byte[] BuildQuery(string name)
    {
        var b = new List<byte> { 0x57, 0x4C, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }; // id, flags, 1 question
        foreach (var label in name.Split('.'))
        {
            b.Add((byte)label.Length);
            b.AddRange(Encoding.ASCII.GetBytes(label));
        }
        b.AddRange(new byte[] { 0, 0, 1, 0, 1 }); // end of name, type A, class IN
        return b.ToArray();
    }

    static IPAddress? ParseA(byte[] m, string want)
    {
        try
        {
            if (m.Length < 12 || (m[2] & 0x80) == 0) return null; // not a response
            int questions = m[4] << 8 | m[5];
            int records = (m[6] << 8 | m[7]) + (m[8] << 8 | m[9]) + (m[10] << 8 | m[11]);
            int p = 12;
            for (int i = 0; i < questions; i++)
            {
                ReadName(m, ref p);
                p += 4;
            }
            for (int i = 0; i < records; i++)
            {
                var name = ReadName(m, ref p);
                int type = m[p] << 8 | m[p + 1];
                int len = m[p + 8] << 8 | m[p + 9];
                p += 10;
                if (type == 1 && len == 4 && name.Equals(want, StringComparison.OrdinalIgnoreCase))
                    return new IPAddress(m.AsSpan(p, 4));
                p += len;
            }
        }
        catch
        {
            // truncated or malformed packet
        }
        return null;
    }

    static string ReadName(byte[] m, ref int p)
    {
        var parts = new List<string>();
        int pos = p;
        bool jumped = false;
        int hops = 0;
        while (true)
        {
            int len = m[pos];
            if (len == 0)
            {
                pos++;
                break;
            }
            if ((len & 0xC0) == 0xC0) // compression pointer
            {
                int target = (len & 0x3F) << 8 | m[pos + 1];
                if (!jumped) p = pos + 2;
                jumped = true;
                pos = target;
                if (++hops > 20) throw new InvalidDataException();
                continue;
            }
            parts.Add(Encoding.ASCII.GetString(m, pos + 1, len));
            pos += 1 + len;
        }
        if (!jumped) p = pos;
        return string.Join('.', parts);
    }
}

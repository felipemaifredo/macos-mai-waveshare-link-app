using System.Diagnostics;
using System.Text.Json.Nodes;

namespace WaveshareLink.Feeds;

/// "Docker" (view only), every 5 s while the page is on screen: `docker ps -a` from the PATH.
///   {"t":"docker","up":bool,"containers":[{"name","image","state","status"}]}
sealed class DockerFeed
{
    const int MaxContainers = 20;

    public Action? OnChange;
    public JsonObject Message { get; private set; } = new() { ["t"] = "docker", ["up"] = false, ["containers"] = new JsonArray() };
    readonly System.Windows.Forms.Timer timer = new() { Interval = 5000 };
    bool busy;

    public DockerFeed() => timer.Tick += (_, _) => _ = PollAsync();

    public void SetActive(bool on)
    {
        if (on && !timer.Enabled)
        {
            timer.Start();
            _ = PollAsync();
        }
        else if (!on)
        {
            timer.Stop();
        }
    }

    async Task PollAsync()
    {
        if (busy) return;
        busy = true;
        try
        {
            var (ok, output) = await Task.Run(Run);
            var list = new List<JsonObject>();
            if (ok)
            {
                foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                {
                    try
                    {
                        if (JsonNode.Parse(line) is not JsonObject c) continue;
                        list.Add(new JsonObject
                        {
                            ["name"] = Text.Ascii(Json.Str(c, "Names")),
                            ["image"] = Text.Ascii(Json.Str(c, "Image")),
                            ["state"] = Json.Str(c, "State") ?? "",
                            ["status"] = Text.Ascii(Json.Str(c, "Status")),
                        });
                    }
                    catch { }
                }
            }
            // Running first, then by name.
            var sorted = list
                .OrderBy(c => Json.Str(c, "state") == "running" ? 0 : 1)
                .ThenBy(c => Json.Str(c, "name"), StringComparer.Ordinal)
                .Take(MaxContainers)
                .Select(c => (JsonNode?)c);
            Message = new JsonObject { ["t"] = "docker", ["up"] = ok, ["containers"] = Json.Array(sorted) };
            OnChange?.Invoke();
        }
        finally
        {
            busy = false;
        }
    }

    static (bool ok, string output) Run()
    {
        try
        {
            var psi = new ProcessStartInfo("docker")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            foreach (var a in new[] { "ps", "-a", "--format", "{{json .}}" }) psi.ArgumentList.Add(a);
            using var p = Process.Start(psi);
            if (p == null) return (false, "");
            var stdout = p.StandardOutput.ReadToEndAsync();
            _ = p.StandardError.ReadToEndAsync();
            if (!p.WaitForExit(10_000))
            {
                try { p.Kill(); } catch { }
                return (false, "");
            }
            return (p.ExitCode == 0, stdout.Result);
        }
        catch
        {
            return (false, ""); // Docker not installed
        }
    }
}

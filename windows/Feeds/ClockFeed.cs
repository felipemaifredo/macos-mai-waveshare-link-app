using System.Globalization;
using System.Text.Json.Nodes;

namespace WaveshareLink.Feeds;

/// "Hora": the board keeps the time itself; we resync it on connect and every minute while the
/// page is on screen. The weather (Open-Meteo, no key) is fetched at most every 30 min.
///   {"t":"clock","epoch","tz","city","temp","min","max","code"}   (weather fields only once fetched)
sealed class ClockFeed
{
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };

    public Action? OnChange;
    readonly Config config;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 60_000 };
    JsonObject weather = new();
    DateTime fetchedAt = DateTime.MinValue;
    DateTime triedAt = DateTime.MinValue;

    public ClockFeed(Config config)
    {
        this.config = config;
        timer.Tick += (_, _) => Minute();
    }

    public JsonObject Message
    {
        get
        {
            var m = new JsonObject
            {
                ["t"] = "clock",
                ["epoch"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
                ["tz"] = (int)TimeZoneInfo.Local.GetUtcOffset(DateTime.Now).TotalSeconds,
                // The board fetches the time and weather on its own over Wi-Fi; this tells it where.
                ["city"] = Text.Ascii(config.City),
                ["lat"] = config.Latitude,
                ["lon"] = config.Longitude,
            };
            foreach (var kv in weather) m[kv.Key] = kv.Value?.DeepClone();
            return m;
        }
    }

    public void SetActive(bool on)
    {
        if (on && !timer.Enabled)
        {
            timer.Start();
            Minute();
        }
        else if (!on)
        {
            timer.Stop();
        }
    }

    /// The city changed in the settings.
    public void Invalidate()
    {
        fetchedAt = triedAt = DateTime.MinValue;
        weather = new JsonObject();
    }

    void Minute()
    {
        OnChange?.Invoke();
        bool stale = DateTime.Now - fetchedAt > TimeSpan.FromMinutes(30);
        bool retry = DateTime.Now - triedAt > TimeSpan.FromMinutes(5);
        if (stale && retry) _ = FetchAsync();
    }

    async Task FetchAsync()
    {
        triedAt = DateTime.Now;
        try
        {
            var url = string.Create(CultureInfo.InvariantCulture,
                $"https://api.open-meteo.com/v1/forecast?latitude={config.Latitude}&longitude={config.Longitude}&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1");
            var root = JsonNode.Parse(await Http.GetStringAsync(url));
            var temp = root?["current"]?["temperature_2m"]?.GetValue<double>();
            if (temp == null) return;
            weather = new JsonObject
            {
                ["city"] = Text.Ascii(config.City),
                ["temp"] = temp.Value,
                ["code"] = root?["current"]?["weather_code"]?.GetValue<int>() ?? -1,
                ["min"] = root?["daily"]?["temperature_2m_min"]?[0]?.GetValue<double>() ?? temp.Value,
                ["max"] = root?["daily"]?["temperature_2m_max"]?[0]?.GetValue<double>() ?? temp.Value,
            };
            fetchedAt = DateTime.Now;
            OnChange?.Invoke();
        }
        catch (Exception e)
        {
            Log.Info($"Tempo (Open-Meteo): {e.Message}");
        }
    }
}

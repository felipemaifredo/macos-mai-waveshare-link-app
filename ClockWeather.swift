import Foundation

/// Time and weather for the "Hora" page.
///   {"t":"clock","epoch","tz","city","lat","lon","temp","min","max","code"}   (weather fields only once fetched)
/// The board also fetches the time and weather on its own over Wi-Fi; lat/lon tell it where.
/// The board keeps the time itself; the Mac resyncs it on connect and every minute while the page
/// is on screen. The weather (Open-Meteo, no key) is fetched at most every 30 min, and only while
/// the page is on screen.
@MainActor
final class ClockWeather {
    static let city = "Cachoeiro de Itapemirim"
    static let latitude = -20.85
    static let longitude = -41.11
    static let url = URL(string: "https://api.open-meteo.com/v1/forecast?latitude=\(latitude)&longitude=\(longitude)"
        + "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
        + "&timezone=auto&forecast_days=1")!
    static let maxAge: TimeInterval = 30 * 60
    static let retryAfter: TimeInterval = 5 * 60

    var onChange: () -> Void = {}
    private var weather: [String: Any] = [:]
    private var fetchedAt: Date?
    private var triedAt: Date?
    private var timer: Timer?

    var message: [String: Any] {
        var m: [String: Any] = [
            "t": "clock",
            "epoch": Int(Date().timeIntervalSince1970),
            "tz": TimeZone.current.secondsFromGMT(),
            "city": Self.city,
            "lat": Self.latitude,
            "lon": Self.longitude,
        ]
        m.merge(weather) { $1 }
        return m
    }

    func setActive(_ on: Bool) {
        if on, timer == nil {
            let t = Timer(timeInterval: 60, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.minute() }
            }
            RunLoop.main.add(t, forMode: .common)
            timer = t
            minute()
        } else if !on {
            timer?.invalidate()
            timer = nil
        }
    }

    private func minute() {
        onChange()
        let stale = fetchedAt.map { Date().timeIntervalSince($0) > Self.maxAge } ?? true
        let retry = triedAt.map { Date().timeIntervalSince($0) > Self.retryAfter } ?? true
        if stale && retry { fetch() }
    }

    private func fetch() {
        triedAt = Date()
        URLSession.shared.dataTask(with: Self.url) { data, _, _ in
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self.parse(data) }
            }
        }.resume()
    }

    private func parse(_ data: Data?) {
        guard let data,
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let current = obj["current"] as? [String: Any],
              let daily = obj["daily"] as? [String: Any],
              let temp = current["temperature_2m"] as? Double else { return }
        weather = [
            "city": Self.city,
            "temp": temp,
            "code": current["weather_code"] as? Int ?? -1,
            "min": (daily["temperature_2m_min"] as? [Double])?.first ?? temp,
            "max": (daily["temperature_2m_max"] as? [Double])?.first ?? temp,
        ]
        fetchedAt = Date()
        onChange()
    }
}

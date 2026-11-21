// Usage providers, the same as in AI Usage (../macos-mai-ai-usage-app/main.swift).
// Keep the two in sync when an API changes.
import AppKit

// MARK: - Model

struct UsageRow {
    let section: String?
    let label: String
    let usedPct: Double
    let resetsAt: Date?
}

enum FetchError: Error {
    case noCredentials
    case expired
    case notRunning
    case http(Int)
    /// HTTP 429: too many requests. Ask again only after this date.
    case rateLimited(until: Date)
    case badResponse
    case network(String)
}

protocol UsageProvider {
    var name: String { get }
    var symbol: String { get }
    var expiredMessage: String { get }
    func fetch() async throws -> [UsageRow]
    /// Value shown in the menu bar title.
    func headline(_ rows: [UsageRow]) -> Double?
}

// MARK: - Helpers

let isoFrac: ISO8601DateFormatter = {
    let f = ISO8601DateFormatter()
    f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return f
}()
let isoPlain = ISO8601DateFormatter()

func parseDate(_ s: String?) -> Date? {
    guard let s else { return nil }
    return isoFrac.date(from: s) ?? isoPlain.date(from: s)
}

/// "14:30" today, "sex. 14:30" on other days.
func shortReset(_ d: Date?) -> String {
    guard let d else { return "" }
    let f = DateFormatter()
    f.locale = Locale(identifier: "pt_BR")
    f.dateFormat = Calendar.current.isDateInToday(d) ? "HH:mm" : "EEE HH:mm"
    return f.string(from: d)
}

func formatReset(_ d: Date?) -> String {
    guard let d else { return "" }
    return " · reseta \(shortReset(d))"
}

func performJSON(_ req: URLRequest) async throws -> [String: Any] {
    let data: Data
    let resp: URLResponse
    do {
        (data, resp) = try await URLSession.shared.data(for: req)
    } catch {
        throw FetchError.network(error.localizedDescription)
    }
    guard let http = resp as? HTTPURLResponse else { throw FetchError.badResponse }
    if http.statusCode == 401 || http.statusCode == 403 { throw FetchError.expired }
    if http.statusCode == 429 {
        // Retry-After in seconds when the API sends it; otherwise wait 15 min.
        let wait = http.value(forHTTPHeaderField: "Retry-After").flatMap(TimeInterval.init) ?? 15 * 60
        throw FetchError.rateLimited(until: Date().addingTimeInterval(max(wait, 60)))
    }
    guard http.statusCode == 200 else { throw FetchError.http(http.statusCode) }
    guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
        throw FetchError.badResponse
    }
    return json
}

// MARK: - Claude

struct ClaudeProvider: UsageProvider {
    let name = "Claude"
    let symbol = "◐"
    let expiredMessage = "Login expirado: abra o Claude Code para renovar"

    func readAccessToken() throws -> String {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/security")
        p.arguments = ["find-generic-password", "-s", "Claude Code-credentials", "-w"]
        let out = Pipe()
        p.standardOutput = out
        p.standardError = Pipe()
        try p.run()
        let data = out.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        guard p.terminationStatus == 0,
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let oauth = json["claudeAiOauth"] as? [String: Any],
              let token = oauth["accessToken"] as? String
        else { throw FetchError.noCredentials }
        if let exp = oauth["expiresAt"] as? Double, exp / 1000 < Date().timeIntervalSince1970 {
            throw FetchError.expired
        }
        return token
    }

    func label(for key: String) -> String {
        switch key {
        case "five_hour": return "Sessão (5h)"
        case "seven_day": return "Semana"
        case "seven_day_opus": return "Semana (Opus)"
        case "seven_day_sonnet": return "Semana (Sonnet)"
        default: return key.replacingOccurrences(of: "_", with: " ").capitalized
        }
    }

    func fetch() async throws -> [UsageRow] {
        let token = try readAccessToken()
        var req = URLRequest(url: URL(string: "https://api.anthropic.com/api/oauth/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.timeoutInterval = 20
        let json = try await performJSON(req)

        let order = ["five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"]
        var items: [(key: String, util: Double, reset: Date?)] = []
        for (key, value) in json {
            guard let obj = value as? [String: Any],
                  let util = (obj["utilization"] as? NSNumber)?.doubleValue else { continue }
            items.append((key, util, parseDate(obj["resets_at"] as? String)))
        }
        items.sort {
            (order.firstIndex(of: $0.key) ?? 99, $0.key) < (order.firstIndex(of: $1.key) ?? 99, $1.key)
        }
        return items.map { UsageRow(section: nil, label: label(for: $0.key), usedPct: $0.util, resetsAt: $0.reset) }
    }

    func headline(_ rows: [UsageRow]) -> Double? {
        (rows.first { $0.label == "Sessão (5h)" } ?? rows.first)?.usedPct
    }
}

// MARK: - Antigravity (local hub API)

struct AntigravityProvider: UsageProvider {
    let name = "Antigravity"
    let symbol = "✦"
    let expiredMessage = "Antigravity recusou o acesso: reabra o Antigravity"

    struct Hub { let port: Int; let csrf: String }

    func findHub() throws -> Hub {
        let dir = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".gemini/antigravity/daemon")
        let files = (try? FileManager.default.contentsOfDirectory(
            at: dir, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        let candidates = files
            .filter { $0.lastPathComponent.hasPrefix("hub_") && $0.pathExtension == "json" }
            .sorted {
                let a = (try? $0.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                let b = (try? $1.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                return a > b
            }
        for url in candidates {
            guard let data = try? Data(contentsOf: url),
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let pid = (json["pid"] as? NSNumber)?.int32Value,
                  let port = (json["httpPort"] as? NSNumber)?.intValue,
                  let csrf = json["csrfToken"] as? String,
                  kill(pid, 0) == 0
            else { continue }
            return Hub(port: port, csrf: csrf)
        }
        throw FetchError.notRunning
    }

    func windowLabel(_ w: String?) -> String {
        switch w {
        case "5h": return "Sessão (5h)"
        case "weekly": return "Semana"
        case let other?: return other
        case nil: return "Limite"
        }
    }

    func fetch() async throws -> [UsageRow] {
        let hub = try findHub()
        var req = URLRequest(url: URL(string:
            "http://127.0.0.1:\(hub.port)/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary")!)
        req.httpMethod = "POST"
        req.httpBody = Data("{}".utf8)
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("1", forHTTPHeaderField: "Connect-Protocol-Version")
        req.setValue(hub.csrf, forHTTPHeaderField: "x-codeium-csrf-token")
        req.timeoutInterval = 20

        let json: [String: Any]
        do {
            json = try await performJSON(req)
        } catch FetchError.network {
            throw FetchError.notRunning
        }
        guard let resp = json["response"] as? [String: Any],
              let groups = resp["groups"] as? [[String: Any]] else { throw FetchError.badResponse }

        var rows: [UsageRow] = []
        for g in groups {
            let section = g["displayName"] as? String
            for b in (g["buckets"] as? [[String: Any]]) ?? [] {
                guard let remaining = (b["remainingFraction"] as? NSNumber)?.doubleValue else { continue }
                rows.append(UsageRow(section: section,
                                     label: windowLabel(b["window"] as? String),
                                     usedPct: max(0, min(100, (1 - remaining) * 100)),
                                     resetsAt: parseDate(b["resetTime"] as? String)))
            }
        }
        return rows
    }

    func headline(_ rows: [UsageRow]) -> Double? {
        (rows.first { $0.label == "Sessão (5h)" || $0.label.contains("5h") } ?? rows.first)?.usedPct
    }
}


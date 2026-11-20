import AppKit
import Carbon.HIToolbox

/// Handles Discord integration: detects voice call activity, channel name,
/// and provides mute/deafen and disconnect actions.
///
/// Board → Mac:
///   {"t":"press","id":"discord:mute"}
///   {"t":"press","id":"discord:deaf"}
///   {"t":"press","id":"discord:disconnect"}
///
/// Mac → Board:
///   {"t":"discord","running":true,"inCall":false,"channel":"geral","muted":false,"deafened":false}
@MainActor
final class DiscordControl {
    var onChange: () -> Void = {}
    private(set) var isRunning = false
    private(set) var inCall = false
    private(set) var channel: String = ""
    private(set) var isMuted = false
    private(set) var isDeafened = false

    private var pollTimer: Timer?

    var message: [String: Any] {
        [
            "t": "discord",
            "running": isRunning,
            "inCall": inCall,
            "channel": SerialLink.ascii(channel),
            "muted": isMuted,
            "deafened": isDeafened
        ]
    }

    func start() {
        checkStatus()
        let t = Timer(timeInterval: 2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.checkStatus() }
        }
        RunLoop.main.add(t, forMode: .common)
        pollTimer = t
    }

    func checkStatus() {
        let running = !NSRunningApplication.runningApplications(withBundleIdentifier: "com.hnc.Discord").isEmpty
        let ch = running ? fetchDiscordChannel() : ""

        var changed = false
        if running != isRunning {
            isRunning = running
            changed = true
        }
        if ch != channel {
            channel = ch
            changed = true
        }
        if !running && inCall {
            inCall = false
            changed = true
        }

        if changed {
            onChange()
        }
    }

    func setCallState(_ active: Bool) {
        if inCall != active {
            inCall = active
            onChange()
        }
    }

    // MARK: - Actions

    func toggleMute() {
        isMuted.toggle()
        sendKeyToDiscord(keyCode: 0x2E, modifiers: [.maskCommand, .maskShift]) // Cmd+Shift+M
        onChange()
    }

    func toggleDeafen() {
        isDeafened.toggle()
        if isDeafened { isMuted = true }
        sendKeyToDiscord(keyCode: 0x02, modifiers: [.maskCommand, .maskShift]) // Cmd+Shift+D
        onChange()
    }

    // MARK: - Helpers

    private func fetchDiscordChannel() -> String {
        let options: CGWindowListOption = [.optionAll]
        guard let list = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] else {
            return ""
        }
        for win in list {
            let owner = win[kCGWindowOwnerName as String] as? String ?? ""
            if owner.lowercased().contains("discord") {
                let title = win[kCGWindowName as String] as? String ?? ""
                if !title.isEmpty && title != "Discord" {
                    // Title format: "#channel | Server - Discord" or "User - Discord"
                    let parts = title.components(separatedBy: " | ")
                    if let first = parts.first, !first.isEmpty {
                        return first.replacingOccurrences(of: " - Discord", with: "").trimmingCharacters(in: .whitespaces)
                    }
                }
            }
        }
        return ""
    }

    private func sendKeyToDiscord(keyCode: CGKeyCode, modifiers: CGEventFlags) {
        if let discordApp = NSRunningApplication.runningApplications(withBundleIdentifier: "com.hnc.Discord").first {
            let pid = discordApp.processIdentifier
            let src = CGEventSource(stateID: .hidSystemState)
            if let down = CGEvent(keyboardEventSource: src, virtualKey: keyCode, keyDown: true),
               let up = CGEvent(keyboardEventSource: src, virtualKey: keyCode, keyDown: false) {
                down.flags = modifiers
                up.flags = modifiers
                down.postToPid(pid)
                up.postToPid(pid)
            }
        }
    }
}

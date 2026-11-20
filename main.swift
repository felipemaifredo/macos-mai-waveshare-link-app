import AppKit
import ServiceManagement

// MARK: - USB display (ESP32-S3 over serial)

/// Talks to the touch display over USB serial using one JSON object per line.
/// Board → Mac: {"t":"hello"} (repeated until it hears from us), {"t":"press","id":"refresh" or "key:<deck key>"},
///              {"t":"page","id":…} when the visible page changes.
/// Mac → Board: the current state of every page on hello, then updates as they happen; {"t":"ping"} every 2 s.
/// Every candidate port is probed; a port that never says hello is skipped until it disappears.
@MainActor
final class SerialLink {
    static let prefixes = ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial"]

    var snapshot: () -> [[String: Any]] = { [] }
    var onPress: (String) -> Void = { _ in }
    var onPage: (String) -> Void = { _ in }
    var onStatusChange: () -> Void = {}
    private(set) var isLinked = false

    private var fd: Int32 = -1
    private(set) var path: String?
    private var openedAt = Date()
    private var source: DispatchSourceRead?
    private var buffer = Data()
    private var outbox = Data()
    private var writer: DispatchSourceWrite?
    private var writerRunning = false
    private var ignored: Set<String> = []
    private var timer: Timer?

    func start() {
        let t = Timer(timeInterval: 2, repeats: true) { _ in
            Task { @MainActor in self.tick() }
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
        tick()
    }

    func candidates() -> [String] {
        var names = ((try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? [])
            .filter { n in Self.prefixes.contains { n.hasPrefix($0) } }
            .sorted()
            .map { "/dev/\($0)" }
        // For testing without the board (e.g. a pty): WAVESHARE_LINK_SERIAL=/dev/ttys012
        if let extra = ProcessInfo.processInfo.environment["WAVESHARE_LINK_SERIAL"] { names.insert(extra, at: 0) }
        return names
    }

    func tick() {
        let present = Set(candidates())
        ignored.formIntersection(present)
        if fd >= 0 {
            if isLinked {
                send(["t": "ping"])
            } else if Date().timeIntervalSince(openedAt) > 6, let path {
                ignored.insert(path) // not our board
                close()
            }
            return
        }
        for p in candidates() where !ignored.contains(p) {
            if open(p) { return }
            ignored.insert(p)
        }
    }

    private func open(_ p: String) -> Bool {
        let f = Darwin.open(p, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard f >= 0 else { return false }
        var t = termios()
        if tcgetattr(f, &t) == 0 {
            cfmakeraw(&t)
            cfsetspeed(&t, speed_t(B115200))
            t.c_cflag |= tcflag_t(CLOCAL | CREAD)
            tcsetattr(f, TCSANOW, &t)
        }
        fd = f
        path = p
        openedAt = Date()
        buffer.removeAll()
        let src = DispatchSource.makeReadSource(fileDescriptor: f, queue: .main)
        src.setEventHandler { [weak self] in
            MainActor.assumeIsolated { self?.readAvailable() }
        }
        src.setCancelHandler { Darwin.close(f) }
        src.resume()
        source = src
        let w = DispatchSource.makeWriteSource(fileDescriptor: f, queue: .main)
        w.setEventHandler { [weak self] in
            MainActor.assumeIsolated { self?.flush() }
        }
        writer = w
        writerRunning = false
        return true
    }

    private func close() {
        if let writer {
            if !writerRunning { writer.resume() } // a suspended source can't be cancelled
            writer.cancel()
        }
        writer = nil
        outbox.removeAll()
        source?.cancel() // closes the fd
        source = nil
        fd = -1
        path = nil
        buffer.removeAll()
        if isLinked {
            isLinked = false
            onStatusChange()
        }
    }

    private func readAvailable() {
        var chunk = [UInt8](repeating: 0, count: 4096)
        let n = read(fd, &chunk, chunk.count)
        if n > 0 {
            buffer.append(contentsOf: chunk[0..<n])
            if buffer.count > 64 * 1024 { buffer.removeAll() } // garbage, not our protocol
            while let nl = buffer.firstIndex(of: 0x0A) {
                let line = buffer[buffer.startIndex..<nl]
                buffer.removeSubrange(buffer.startIndex...nl)
                handle(Data(line))
            }
        } else if n == 0 || (errno != EAGAIN && errno != EINTR) {
            close() // unplugged
        }
    }

    private func handle(_ line: Data) {
        guard let obj = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
              let type = obj["t"] as? String else { return } // boot logs and other noise
        switch type {
        case "hello":
            if !isLinked {
                isLinked = true
                onStatusChange()
            }
            snapshot().forEach(send)
        case "press":
            if isLinked, let id = obj["id"] as? String { onPress(id) }
        case "page":
            if isLinked, let id = obj["id"] as? String { onPage(id) }
        default:
            break
        }
    }

    func send(_ obj: [String: Any]) {
        guard fd >= 0, var data = try? JSONSerialization.data(withJSONObject: obj) else { return }
        data.append(0x0A)
        // The UART port runs at 115200 baud, so a cover (~20 KB) takes ~2 s: queue it instead of blocking.
        if outbox.count + data.count > 512 * 1024 { return } // the board stopped reading
        outbox.append(data)
        flush()
    }

    private func flush() {
        while !outbox.isEmpty {
            let w = outbox.withUnsafeBytes { write(fd, $0.baseAddress!, $0.count) }
            if w > 0 {
                outbox.removeFirst(w)
            } else if w < 0 && errno != EAGAIN && errno != EINTR {
                outbox.removeAll()
            } else {
                break
            }
        }
        // Wake up when the port can take more, and only then.
        if outbox.isEmpty, writerRunning {
            writer?.suspend()
            writerRunning = false
        } else if !outbox.isEmpty, !writerRunning {
            writer?.resume()
            writerRunning = true
        }
    }

    /// The board's fonts are ASCII only.
    static func ascii(_ s: String) -> String {
        let stripped = s.applyingTransform(.stripDiacritics, reverse: false) ?? s
        return String(stripped.unicodeScalars.map { $0.isASCII ? Character($0) : "?" })
    }
}

// MARK: - Deck actions (the "Atalhos" page on the display)

@MainActor
enum DeckActions {
    // NX_KEYTYPE_* from IOKit/hidsystem/ev_keymap.h
    static let mediaKeys: [String: Int] = [
        "volup": 0, "voldown": 1, "mute": 7, "play": 16, "next": 17, "prev": 18,
    ]

    static func run(_ id: String) {
        if let key = mediaKeys[id] {
            postMediaKey(key)
            return
        }
        switch id {
        case "shot":
            // The same toolbar as ⌘⇧5.
            let url = URL(fileURLWithPath: "/System/Applications/Utilities/Screenshot.app")
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
        case "sleep":
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/pmset")
            p.arguments = ["displaysleepnow"]
            try? p.run()
        default:
            break
        }
    }

    /// Posting keyboard events needs Accessibility permission; the first press asks for it.
    /// The app is signed ad-hoc, so every rebuild needs it granted again.
    static func postMediaKey(_ key: Int) {
        let opts = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
        guard AXIsProcessTrustedWithOptions(opts) else { return }
        for down in [true, false] {
            let state = down ? 0xA : 0xB
            let ev = NSEvent.otherEvent(
                with: .systemDefined, location: .zero,
                modifierFlags: NSEvent.ModifierFlags(rawValue: UInt(state << 8)),
                timestamp: 0, windowNumber: 0, context: nil,
                subtype: 8, data1: (key << 16) | (state << 8), data2: -1)
            ev?.cgEvent?.post(tap: .cghidEventTap)
        }
    }
}


// MARK: - Usage from the AI Usage app

/// AI Usage (local.aiusagebar) announces its numbers with distributed notifications:
///   local.aiusagebar.usage    AI Usage → us   userInfo["json"] = {"providers":[…]}
///   local.aiusagebar.request  us → AI Usage   "send me the current numbers"
///   local.aiusagebar.refresh  us → AI Usage   "fetch now" (the board's refresh button)
/// Without AI Usage running the board still works; the usage page just has no data.
@MainActor
final class UsageFeed {
    static let appID = "local.aiusagebar"
    static let usageName = Notification.Name("local.aiusagebar.usage")
    static let requestName = Notification.Name("local.aiusagebar.request")
    static let refreshName = Notification.Name("local.aiusagebar.refresh")

    private(set) var providers: [Any]?
    var onChange: () -> Void = {}

    var isAppRunning: Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: Self.appID).isEmpty
    }

    func start() {
        DistributedNotificationCenter.default().addObserver(
            forName: Self.usageName, object: nil, queue: .main
        ) { [weak self] n in
            let json = n.userInfo?["json"] as? String
            MainActor.assumeIsolated { self?.receive(json) }
        }
        post(Self.requestName)
    }

    /// Asks AI Usage to fetch now, opening it first if it is closed (it fetches on launch).
    func refresh() {
        if isAppRunning {
            post(Self.refreshName)
        } else if let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: Self.appID) {
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
        }
    }

    /// The board message for the current numbers, or nil before the first one arrives.
    func boardMessage() -> [String: Any]? {
        guard let providers else { return nil }
        return ["t": "usage", "providers": Self.asciiDeep(providers)]
    }

    private func receive(_ json: String?) {
        guard let data = json?.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let list = obj["providers"] as? [Any] else { return }
        providers = list
        onChange()
    }

    private func post(_ name: Notification.Name) {
        DistributedNotificationCenter.default().postNotificationName(
            name, object: nil, userInfo: nil, deliverImmediately: true)
    }

    private static func asciiDeep(_ v: Any) -> Any {
        switch v {
        case let s as String: return SerialLink.ascii(s)
        case let a as [Any]: return a.map(asciiDeep)
        case let d as [String: Any]: return d.mapValues(asciiDeep)
        default: return v
        }
    }
}

// MARK: - App

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    let device = SerialLink()
    let usage = UsageFeed()
    let nowPlaying = NowPlaying()
    let mic = MicControl()
    let stats = SystemStats()
    let docker = DockerFeed()
    let clock = ClockWeather()
    let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    let menu = NSMenu()

    func applicationDidFinishLaunching(_ n: Notification) {
        statusItem.button?.image = NSImage(systemSymbolName: "display", accessibilityDescription: "Waveshare Link")
        menu.delegate = self
        menu.autoenablesItems = false
        statusItem.menu = menu
        statusItem.button?.appearsDisabled = true

        usage.onChange = { [weak self] in self?.push(self?.usage.boardMessage()) }
        usage.start()
        nowPlaying.onChange = { [weak self] in self?.push(self?.nowPlaying.message) }
        nowPlaying.onArt = { [weak self] in self?.push(self?.nowPlaying.artMessage) }
        nowPlaying.start()
        mic.onChange = { [weak self] in self?.push(self?.mic.message) }
        mic.start()
        stats.onSample = { [weak self] in self?.push($0) }
        docker.onChange = { [weak self] in self?.push(self?.docker.message) }
        clock.onChange = { [weak self] in self?.push(self?.clock.message) }

        // Everything the board shows, sent again whenever it (re)connects.
        device.snapshot = { [weak self] in
            guard let self else { return [] }
            return [usage.boardMessage(), nowPlaying.message, nowPlaying.artMessage, mic.message, clock.message]
                .compactMap { $0 }
        }
        device.onPress = { [weak self] id in
            if id == "refresh" { self?.usage.refresh() }
            else if id == "key:mic" { self?.mic.toggle() }
            else if id.hasPrefix("key:") { DeckActions.run(String(id.dropFirst(4))) }
        }
        device.onPage = { [weak self] id in self?.pageChanged(id) }
        device.onStatusChange = { [weak self] in self?.linkChanged() }
        device.start()
    }

    func push(_ msg: [String: Any]?) {
        if device.isLinked, let msg { device.send(msg) }
    }

    /// Only the visible page's data is gathered, so the hidden pages cost nothing.
    func pageChanged(_ id: String) {
        stats.setActive(id == "mac")
        docker.setActive(id == "docker")
        clock.setActive(id == "clock")
    }

    func linkChanged() {
        statusItem.button?.appearsDisabled = !device.isLinked
        mic.setActive(device.isLinked)
        if !device.isLinked { pageChanged("") }
    }

    // Rebuilt every time it opens, so it always shows the current state.
    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()

        if device.isLinked, let path = device.path {
            menu.addItem(disabled("Placa: conectada (\((path as NSString).lastPathComponent))"))
        } else {
            menu.addItem(disabled("Placa: procurando no USB…"))
        }

        menu.addItem(.separator())
        let login = NSMenuItem(title: "Abrir ao iniciar sessão", action: #selector(toggleLogin), keyEquivalent: "")
        login.target = self
        login.state = SMAppService.mainApp.status == .enabled ? .on : .off
        menu.addItem(login)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Sair", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
    }

    func disabled(_ title: String) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        i.isEnabled = false
        return i
    }

    @objc func toggleLogin() {
        if SMAppService.mainApp.status == .enabled {
            try? SMAppService.mainApp.unregister()
        } else {
            try? SMAppService.mainApp.register()
        }
    }
}

MainActor.assumeIsolated {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.accessory)
    app.run()
}

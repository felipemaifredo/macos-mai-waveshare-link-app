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
            // State first (the layout leads it), then the status change, which starts the feeds.
            let wasLinked = isLinked
            isLinked = true
            snapshot().forEach(send)
            if !wasLinked { onStatusChange() }
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


// MARK: - AI usage (fetched here, the same way AI Usage does; see UsageProviders.swift)

/// Fetches Claude and Antigravity usage for the board's "Uso" page: on connect, every 5 min
/// and when the board's ↻ is pressed. Nothing is fetched while the board is disconnected.
///   {"t":"usage","providers":[{"name","pct","err","busy","at","rows":[{"sec","label","pct","reset"}]}]}
@MainActor
final class UsageFeed {
    final class Entry {
        let provider: UsageProvider
        var rows: [UsageRow] = []
        var error: String?
        var updatedAt: Date?
        var busy = false
        init(_ p: UsageProvider) { provider = p }
    }

    static let interval: TimeInterval = 300

    var onChange: () -> Void = {}
    private let entries = [Entry(ClaudeProvider()), Entry(AntigravityProvider())]
    private var timer: Timer?

    func setActive(_ on: Bool) {
        if on, timer == nil {
            refresh()
        } else if !on {
            timer?.invalidate()
            timer = nil
        }
    }

    func refresh() {
        timer?.invalidate()
        let t = Timer(timeInterval: Self.interval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t

        for e in entries where !e.busy {
            e.busy = true
            Task {
                do {
                    e.rows = try await e.provider.fetch()
                    e.updatedAt = Date()
                    e.error = nil
                } catch FetchError.notRunning {
                    e.rows = []
                    e.error = "Antigravity não está aberto"
                } catch FetchError.expired {
                    e.error = e.provider.expiredMessage
                } catch FetchError.noCredentials {
                    e.error = "Credenciais do Claude Code não encontradas"
                } catch FetchError.http(let code) {
                    e.error = "Erro HTTP \(code)"
                } catch FetchError.network(let msg) {
                    e.error = "Sem conexão: \(msg)"
                } catch {
                    e.error = "Resposta inesperada da API"
                }
                e.busy = false
                self.onChange()
            }
        }
        onChange() // shows "Atualizando..." right away
    }

    func boardMessage() -> [String: Any] {
        let f = DateFormatter()
        f.dateFormat = "HH:mm"
        let providers: [[String: Any]] = entries.map { e in
            [
                "name": e.provider.name,
                "pct": e.provider.headline(e.rows).map { Int($0.rounded()) } ?? -1,
                "err": SerialLink.ascii(e.error ?? ""),
                "busy": e.busy,
                "at": e.updatedAt.map { f.string(from: $0) } ?? "",
                "rows": e.rows.map { r in [
                    "sec": SerialLink.ascii(r.section ?? ""),
                    "label": SerialLink.ascii(r.label),
                    "pct": Int(r.usedPct.rounded()),
                    "reset": SerialLink.ascii(shortReset(r.resetsAt)),
                ] as [String: Any] },
            ]
        }
        return ["t": "usage", "providers": providers]
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
            // The layout first, so the board has its tabs and keys before the data arrives.
            return [Settings.shared.layoutMessage, usage.boardMessage(), nowPlaying.message,
                    nowPlaying.artMessage, mic.message, clock.message]
        }
        device.onPress = { [weak self] id in
            if id == "refresh" { self?.usage.refresh() }
            else if id.hasPrefix("key:") { self?.pressKey(String(id.dropFirst(4))) }
        }
        Settings.shared.onChange = { [weak self] in
            guard let self else { return }
            push(Settings.shared.layoutMessage)
            usage.setActive(device.isLinked && Settings.shared.isEnabled("usage"))
        }
        device.onPage = { [weak self] id in self?.pageChanged(id) }
        device.onStatusChange = { [weak self] in self?.linkChanged() }
        device.start()
    }

    /// Keys from the Atalhos page (configured in Settings) and the media buttons of the Tocando page.
    func pressKey(_ id: String) {
        let action = Settings.shared.key(boardID: id)
        if id == "mic" || (action?.kind == "builtin" && action?.value == "mic") {
            mic.toggle()
        } else if let action {
            KeyRunner.run(action)
        } else {
            DeckActions.run(id)
        }
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
        usage.setActive(device.isLinked && Settings.shared.isEnabled("usage"))
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
        let settings = NSMenuItem(title: "Ajustes…", action: #selector(openSettings), keyEquivalent: ",")
        settings.target = self
        menu.addItem(settings)
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

    @objc func openSettings() {
        SettingsWindow.shared.show()
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

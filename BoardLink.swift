import AppKit
import CryptoKit

/// What the Settings window and the menu show about the connection.
@MainActor
final class LinkStatus: ObservableObject {
    static let shared = LinkStatus()
    /// A board answers on a USB port (needed to send the Wi-Fi settings).
    @Published var usbPresent = false
    /// "Wi-Fi", "USB" or nil.
    @Published var via: String?
    @Published var address: String?
    /// The board shows this Mac (another host may be on screen).
    @Published var active = false
    /// The board on the network does not know this Mac yet.
    @Published var needsPairing = false
    /// The board's own network: "off", "connecting" or "connected", with ssid, ip and rssi.
    @Published var boardNet: [String: Any] = [:]
    @Published var problem: String?
}

/// The connection to the board, over Wi-Fi when it can and over USB otherwise.
///
/// Handshake (protocol v2, see README):
///   Board → host: {"t":"hello","v":2,"id":<board id>,"nonce":<Wi-Fi only>}
///   Host → board: {"t":"auth","hostId","name","os"[,"nonce","hmac"]}; over Wi-Fi the HMAC proves we
///                 hold the token from pairing, and the board proves it back with its own HMAC.
///   Board → host: {"t":"auth","ok"[,"hmac"]}, then {"t":"active","on"}: only the active host's data
///                 reaches the screen, so the collectors only run while active.
///   Pairing (Wi-Fi, first time): {"t":"pair"} → the board shows a code → {"t":"pair","code"} →
///                 {"t":"paired","token"}.
/// While the Wi-Fi link is up the USB port stays quiet (we said "bye"), so the board does not list
/// this Mac twice. USB v1 boards (no "v":2) are linked and active straight away.
@MainActor
final class BoardLink {
    enum Via { case usb, wifi }

    struct Session {
        var linked = false
        var active = false
    }

    let serial = SerialLink()
    let net = NetLink()

    var snapshot: () -> [[String: Any]] = { [] }
    var onPress: (String) -> Void = { _ in }
    var onPage: (String) -> Void = { _ in }
    var onStatusChange: () -> Void = {}

    private var usb = Session()
    private var wifi = Session()
    private var boardId: String?
    private var boardNonce = ""
    private var myNonce = ""
    private var promptedPairing = false
    private var askingCode = false
    private var timer: Timer?

    var via: Via? { wifi.linked ? .wifi : usb.linked ? .usb : nil }
    /// Linked and on screen: the only state in which we gather data and send it.
    var isLive: Bool { wifi.linked ? wifi.active : (usb.linked && usb.active) }
    var isLinked: Bool { via != nil }

    // MARK: Identity

    static var hostId: String {
        let d = UserDefaults.standard
        if let id = d.string(forKey: "hostId") { return id }
        let id = UUID().uuidString
        d.set(id, forKey: "hostId")
        return id
    }

    private var tokens: [String: String] {
        get { UserDefaults.standard.dictionary(forKey: "boardTokens") as? [String: String] ?? [:] }
        set { UserDefaults.standard.set(newValue, forKey: "boardTokens") }
    }

    private var hostName: String {
        let n = SerialLink.ascii(Settings.shared.hostName.trimmingCharacters(in: .whitespaces))
        return String((n.isEmpty ? "Mac" : n).prefix(23))
    }

    // MARK: Start

    func start() {
        serial.onMessage = { [weak self] in self?.handle($0, from: .usb) }
        serial.onClose = { [weak self] in self?.usbClosed() }
        net.onMessage = { [weak self] in self?.handle($0, from: .wifi) }
        net.onClose = { [weak self] in self?.wifiClosed() }
        serial.start()
        applySettings()

        let t = Timer(timeInterval: 2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    /// Settings → Conexão changed.
    func applySettings() {
        net.configure(enabled: Settings.shared.useWifi, address: Settings.shared.boardAddress)
    }

    /// The name shown on the board changed.
    func renamed() {
        send(["t": "name", "name": hostName])
    }

    private func tick() {
        if wifi.linked {
            net.send(["t": "ping"])
            // The board answers every ping; silence means it is gone (power cut, out of range).
            if Date().timeIntervalSince(net.lastRx) > 8 { net.close() }
        } else if usb.linked {
            serial.send(["t": "ping"])
        }
        publish()
    }

    func send(_ obj: [String: Any]) {
        switch via {
        case .wifi: net.send(obj)
        case .usb: serial.send(obj)
        case nil: break
        }
    }

    /// Sends the network the board should join. Only over USB: the board refuses it over Wi-Fi.
    @discardableResult
    func sendWifi(ssid: String, pass: String) -> Bool {
        guard serial.confirmed else { return false }
        serial.send(["t": "wifi", "ssid": ssid, "pass": pass])
        return true
    }

    // MARK: Messages

    private func handle(_ obj: [String: Any], from v: Via) {
        guard let t = obj["t"] as? String else { return }
        switch t {
        case "hello":
            v == .usb ? usbHello(obj) : wifiHello(obj)
        case "auth":
            v == .usb ? usbAuth(obj) : wifiAuth(obj)
        case "pair":
            if v == .wifi { pairState(obj["state"] as? String ?? "") }
        case "paired":
            if v == .wifi { paired(obj) }
        case "active":
            setActive(obj["on"] as? Bool ?? false, on: v)
        case "net":
            LinkStatus.shared.boardNet = obj
        case "press":
            if v == via, isLive, let id = obj["id"] as? String { onPress(id) }
        case "page":
            if v == via, isLive, let id = obj["id"] as? String { onPage(id) }
        default:
            break
        }
    }

    private func authMessage(wifi: Bool) -> [String: Any] {
        var m: [String: Any] = ["t": "auth", "hostId": Self.hostId, "name": hostName, "os": "macos"]
        if wifi, let boardId, let token = tokens[boardId] {
            myNonce = Self.randomHex(8)
            m["nonce"] = myNonce
            m["hmac"] = Self.hmac(token, "host:" + boardNonce)
        }
        return m
    }

    // MARK: USB

    private func usbHello(_ obj: [String: Any]) {
        publish()
        if wifi.linked { return } // Wi-Fi carries the link; USB stays quiet
        if (obj["v"] as? Int ?? 1) >= 2 {
            serial.send(authMessage(wifi: false)) // linked on the answer, active when the board says so
        } else {
            // v1 board: no auth, no active host.
            usb = Session(linked: true, active: true)
            snapshot().forEach(serial.send)
            changed()
        }
    }

    private func usbAuth(_ obj: [String: Any]) {
        guard !wifi.linked, obj["ok"] as? Bool == true else { return }
        if !usb.linked {
            usb.linked = true
            changed()
        }
    }

    private func usbClosed() {
        usb = Session()
        changed()
    }

    // MARK: Wi-Fi

    private func wifiHello(_ obj: [String: Any]) {
        guard let id = obj["id"] as? String else { return }
        boardId = id
        boardNonce = obj["nonce"] as? String ?? ""
        if tokens[id] != nil {
            net.send(authMessage(wifi: true))
        } else {
            needsPairing()
        }
    }

    private func wifiAuth(_ obj: [String: Any]) {
        guard let boardId else { return }
        guard obj["ok"] as? Bool == true else {
            // The board forgot us (its pairings were erased): pair again.
            tokens[boardId] = nil
            needsPairing()
            return
        }
        guard let token = tokens[boardId],
              let proof = obj["hmac"] as? String,
              proof == Self.hmac(token, "board:" + myNonce) else {
            // Something on the network pretends to be the board. Its presses would run here.
            LinkStatus.shared.problem = "A placa na rede não provou a identidade. Conexão recusada."
            net.close()
            return
        }
        wifiLinked()
    }

    private func wifiLinked() {
        LinkStatus.shared.needsPairing = false
        LinkStatus.shared.problem = nil
        wifi.linked = true
        if usb.linked {
            // One link per Mac: the board drops the USB host and, if it was on screen, puts us there.
            serial.send(["t": "bye"])
            usb = Session()
        }
        changed()
    }

    private func wifiClosed() {
        let was = wifi.linked
        wifi = Session()
        askingCode = false
        // USB takes over on the board's next hello.
        if was { changed() } else { publish() }
    }

    // MARK: Pairing

    private func needsPairing() {
        LinkStatus.shared.needsPairing = true
        publish()
        if !promptedPairing {
            promptedPairing = true
            startPairing()
        }
    }

    /// Asks the board to show a code (the menu's "Parear pelo Wi-Fi…" and the first connection).
    func startPairing() {
        guard net.isOpen, !wifi.linked else {
            LinkStatus.shared.problem = "A placa não foi encontrada na rede."
            return
        }
        net.send(["t": "pair", "hostId": Self.hostId, "name": hostName, "os": "macos"])
    }

    private func pairState(_ state: String) {
        switch state {
        case "code": askCode("Digite o código de 6 dígitos que apareceu na tela da placa.")
        case "wrong": askCode("Código errado. Confira na tela da placa e tente de novo.")
        case "busy": LinkStatus.shared.problem = "Outro computador está pareando com a placa agora."
        case "failed": LinkStatus.shared.problem = "Código errado 3 vezes. Use \"Parear pelo Wi-Fi…\" no menu."
        case "nocode": startPairing()
        default: break // "cancelled": the board closes the connection
        }
    }

    private func askCode(_ message: String) {
        guard !askingCode else { return }
        askingCode = true
        // Out of the network callback before running a modal loop.
        DispatchQueue.main.async { [weak self] in
            MainActor.assumeIsolated {
                guard let self else { return }
                let alert = NSAlert()
                alert.messageText = "Parear com a tela Waveshare"
                alert.informativeText = message
                let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 160, height: 24))
                field.placeholderString = "000000"
                field.font = .monospacedDigitSystemFont(ofSize: 15, weight: .regular)
                alert.accessoryView = field
                alert.addButton(withTitle: "Parear")
                alert.addButton(withTitle: "Cancelar")
                alert.window.initialFirstResponder = field
                NSApp.activate(ignoringOtherApps: true)
                let ok = alert.runModal() == .alertFirstButtonReturn
                self.askingCode = false
                guard self.net.isOpen, !self.wifi.linked else { return }
                if ok {
                    let code = field.stringValue.filter(\.isNumber)
                    self.net.send(["t": "pair", "code": code, "hostId": Self.hostId, "name": self.hostName, "os": "macos"])
                } else {
                    self.net.close() // the board hides the code when we leave
                }
            }
        }
    }

    private func paired(_ obj: [String: Any]) {
        guard let token = obj["token"] as? String, let id = (obj["id"] as? String) ?? boardId else { return }
        tokens[id] = token
        wifiLinked()
    }

    // MARK: Active host

    private func setActive(_ on: Bool, on v: Via) {
        switch v {
        case .usb:
            guard usb.linked, !wifi.linked else { return }
            usb.active = on
        case .wifi:
            guard wifi.linked else { return }
            wifi.active = on
        }
        // On screen again: the board needs our layout and every page's state.
        if on { snapshot().forEach(send) }
        changed()
    }

    private func changed() {
        publish()
        onStatusChange()
    }

    private func publish() {
        let s = LinkStatus.shared
        s.usbPresent = serial.confirmed
        s.via = via == .wifi ? "Wi-Fi" : via == .usb ? "USB" : nil
        s.address = via == .wifi ? net.address : serial.path.map { ($0 as NSString).lastPathComponent }
        s.active = isLive
    }

    // MARK: Crypto

    static func hmac(_ key: String, _ message: String) -> String {
        let mac = HMAC<SHA256>.authenticationCode(for: Data(message.utf8), using: SymmetricKey(data: Data(key.utf8)))
        return mac.map { String(format: "%02x", $0) }.joined()
    }

    static func randomHex(_ bytes: Int) -> String {
        (0..<bytes).map { _ in String(format: "%02x", UInt8.random(in: 0...255)) }.joined()
    }
}

import Foundation
import Network

/// The Wi-Fi transport: a TCP connection to the board, one JSON object per line (the same as USB).
/// The board announces itself over Bonjour as `_wslink._tcp`; an address typed in Settings wins
/// (for networks where mDNS does not get through). It only moves messages; BoardLink does the handshake.
@MainActor
final class NetLink {
    static let service = "_wslink._tcp"
    static let port: UInt16 = 7270

    var onOpen: () -> Void = {}
    var onMessage: ([String: Any]) -> Void = { _ in }
    var onClose: () -> Void = {}
    private(set) var isOpen = false
    private(set) var address: String?
    private(set) var lastRx = Date()

    private var enabled = false
    private var manualAddress = ""
    private var browser: NWBrowser?
    private var found: [NWEndpoint] = []
    private var conn: NWConnection?
    private var buffer = Data()
    private var retry: Timer?

    func configure(enabled on: Bool, address: String) {
        let addr = address.trimmingCharacters(in: .whitespaces)
        guard on != enabled || addr != manualAddress else { return }
        enabled = on
        manualAddress = addr
        close()
        browser?.cancel()
        browser = nil
        found = []
        if on {
            if addr.isEmpty { startBrowser() }
            connectIfIdle()
        }
    }

    func send(_ obj: [String: Any]) {
        guard isOpen, let conn, var data = try? JSONSerialization.data(withJSONObject: obj) else { return }
        data.append(0x0A)
        conn.send(content: data, completion: .contentProcessed { _ in })
    }

    /// Drops the connection; it comes back by itself in a few seconds.
    func close() {
        conn?.cancel()
        conn = nil
        buffer.removeAll()
        address = nil
        if isOpen {
            isOpen = false
            onClose()
        }
        scheduleRetry()
    }

    private func startBrowser() {
        let b = NWBrowser(for: .bonjour(type: Self.service, domain: nil), using: .tcp)
        b.browseResultsChangedHandler = { [weak self] results, _ in
            MainActor.assumeIsolated {
                self?.found = results.map(\.endpoint)
                self?.connectIfIdle()
            }
        }
        b.stateUpdateHandler = { [weak self] state in
            if case .failed = state {
                MainActor.assumeIsolated {
                    // e.g. the network went away: start over in a while.
                    self?.browser?.cancel()
                    self?.browser = nil
                    self?.scheduleRetry()
                }
            }
        }
        b.start(queue: .main)
        browser = b
    }

    private func scheduleRetry() {
        retry?.invalidate()
        guard enabled else { return }
        let t = Timer(timeInterval: 3, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, self.enabled else { return }
                if self.manualAddress.isEmpty, self.browser == nil { self.startBrowser() }
                self.connectIfIdle()
            }
        }
        RunLoop.main.add(t, forMode: .common)
        retry = t
    }

    private func connectIfIdle() {
        guard enabled, conn == nil else { return }
        let endpoint: NWEndpoint
        if !manualAddress.isEmpty, let port = NWEndpoint.Port(rawValue: Self.port) {
            endpoint = .hostPort(host: NWEndpoint.Host(manualAddress), port: port)
        } else if let first = found.first {
            endpoint = first
        } else {
            return
        }

        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true // presses and slider moves are tiny and should go out at once
        tcp.connectionTimeout = 5
        tcp.enableKeepalive = true
        tcp.keepaliveIdle = 5
        let c = NWConnection(to: endpoint, using: NWParameters(tls: nil, tcp: tcp))
        conn = c
        c.stateUpdateHandler = { [weak self] state in
            MainActor.assumeIsolated { self?.stateChanged(c, state) }
        }
        c.start(queue: .main)
    }

    private func stateChanged(_ c: NWConnection, _ state: NWConnection.State) {
        guard c === conn else { return }
        switch state {
        case .ready:
            isOpen = true
            lastRx = Date()
            buffer.removeAll()
            if case let .hostPort(host, _) = c.currentPath?.remoteEndpoint {
                address = "\(host)".components(separatedBy: "%").first
            }
            receive(c)
            onOpen()
        case .waiting, .failed:
            close()
        case .cancelled:
            if c === conn { close() }
        default:
            break
        }
    }

    private func receive(_ c: NWConnection) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            MainActor.assumeIsolated {
                guard let self, c === self.conn else { return }
                if let data, !data.isEmpty {
                    self.lastRx = Date()
                    self.buffer.append(data)
                    if self.buffer.count > 128 * 1024 { self.buffer.removeAll() }
                    while let nl = self.buffer.firstIndex(of: 0x0A) {
                        let line = self.buffer[self.buffer.startIndex..<nl]
                        self.buffer.removeSubrange(self.buffer.startIndex...nl)
                        if let obj = try? JSONSerialization.jsonObject(with: Data(line)) as? [String: Any] {
                            self.onMessage(obj)
                            guard c === self.conn else { return } // closed while handling
                        }
                    }
                }
                if isComplete || error != nil {
                    self.close()
                } else {
                    self.receive(c)
                }
            }
        }
    }
}

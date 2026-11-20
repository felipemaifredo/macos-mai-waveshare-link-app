import Foundation

/// The Docker containers for the "Docker" page (view only), every 5 s while that page is on screen.
///   {"t":"docker","up":bool,"containers":[{"name","image","state","status"}]}
@MainActor
final class DockerFeed {
    static let paths = [
        "/usr/local/bin/docker",
        "/opt/homebrew/bin/docker",
        "/Applications/Docker.app/Contents/Resources/bin/docker",
    ]
    static let maxContainers = 20

    var onChange: () -> Void = {}
    private(set) var message: [String: Any] = ["t": "docker", "up": false, "containers": []]
    private var timer: Timer?
    private var busy = false

    func setActive(_ on: Bool) {
        if on, timer == nil {
            let t = Timer(timeInterval: 5, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.poll() }
            }
            RunLoop.main.add(t, forMode: .common)
            timer = t
            poll()
        } else if !on {
            timer?.invalidate()
            timer = nil
        }
    }

    private func poll() {
        guard !busy else { return }
        guard let exe = Self.paths.first(where: FileManager.default.isExecutableFile) else {
            apply(ok: false, output: Data())
            return
        }
        busy = true
        DispatchQueue.global(qos: .utility).async {
            let p = Process()
            p.executableURL = URL(fileURLWithPath: exe)
            p.arguments = ["ps", "-a", "--format", "{{json .}}"]
            let out = Pipe()
            p.standardOutput = out
            p.standardError = FileHandle.nullDevice
            var data = Data()
            var ok = false
            if (try? p.run()) != nil {
                data = out.fileHandleForReading.readDataToEndOfFile()
                p.waitUntilExit()
                ok = p.terminationStatus == 0
            }
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    self.busy = false
                    self.apply(ok: ok, output: data)
                }
            }
        }
    }

    private func apply(ok: Bool, output: Data) {
        var list: [[String: Any]] = []
        if ok {
            for line in output.split(separator: 0x0A) {
                guard let c = try? JSONSerialization.jsonObject(with: Data(line)) as? [String: Any] else { continue }
                list.append([
                    "name": SerialLink.ascii(c["Names"] as? String ?? ""),
                    "image": SerialLink.ascii(c["Image"] as? String ?? ""),
                    "state": c["State"] as? String ?? "",
                    "status": SerialLink.ascii(c["Status"] as? String ?? ""),
                ])
            }
            // Running first, then by name.
            list.sort {
                let a = $0["state"] as? String == "running", b = $1["state"] as? String == "running"
                return a != b ? a : ($0["name"] as? String ?? "") < ($1["name"] as? String ?? "")
            }
        }
        message = ["t": "docker", "up": ok, "containers": Array(list.prefix(Self.maxContainers))]
        onChange()
    }
}

import AppKit
import ImageIO

/// What Music or Spotify is playing, for the "Tocando" page.
///   {"t":"now","app","title","artist","album","playing","pos","dur"}   app "" = nothing playing
///   {"t":"art","jpg":"<base64 220x220 JPEG>"}                          no jpg = no cover
/// Both players announce every change with a distributed notification, so nothing is polled.
/// The details come from AppleScript, which asks for Automation permission once per player.
@MainActor
final class NowPlaying {
    struct Player {
        let name: String
        let bundleID: String
        let notification: String
        let script: String
    }

    static let players = [
        Player(name: "Spotify", bundleID: "com.spotify.client", notification: "com.spotify.client.PlaybackStateChanged", script: """
            tell application id "com.spotify.client"
                if player state is stopped then return {"stopped"}
                set t to current track
                return {player state as text, name of t, artist of t, album of t, player position, (duration of t) / 1000, id of t}
            end tell
            """),
        Player(name: "Music", bundleID: "com.apple.Music", notification: "com.apple.Music.playerInfo", script: """
            tell application id "com.apple.Music"
                if player state is stopped then return {"stopped"}
                set t to current track
                return {player state as text, name of t, artist of t, album of t, player position, duration of t, persistent ID of t}
            end tell
            """),
    ]
    static let spotifyArt = #"tell application id "com.spotify.client" to return artwork url of current track"#
    static let musicArt = #"tell application id "com.apple.Music" to return raw data of artwork 1 of current track"#
    static let artSide = 220

    var onChange: () -> Void = {}
    var onArt: () -> Void = {}
    private(set) var message: [String: Any] = ["t": "now", "app": ""]
    private(set) var artMessage: [String: Any] = ["t": "art"]
    private var trackKey = ""
    private var scripts: [String: NSAppleScript] = [:]

    func start() {
        for p in Self.players {
            DistributedNotificationCenter.default().addObserver(
                forName: Notification.Name(p.notification), object: nil, queue: .main
            ) { [weak self] _ in
                MainActor.assumeIsolated { self?.update(p) }
            }
        }
        NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didTerminateApplicationNotification, object: nil, queue: .main
        ) { [weak self] n in
            let id = (n.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication)?.bundleIdentifier
            MainActor.assumeIsolated {
                guard let self, let p = Self.players.first(where: { $0.bundleID == id }) else { return }
                if self.message["app"] as? String == p.name { self.clear() }
            }
        }
        // Whatever is already playing at launch.
        for p in Self.players where isRunning(p) {
            update(p)
            if message["playing"] as? Bool == true { break }
        }
    }

    private func isRunning(_ p: Player) -> Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: p.bundleID).isEmpty
    }

    private func run(_ source: String) -> NSAppleEventDescriptor? {
        let script = scripts[source] ?? NSAppleScript(source: source)
        scripts[source] = script
        var error: NSDictionary?
        let result = script?.executeAndReturnError(&error)
        return error == nil ? result : nil
    }

    private func update(_ p: Player) {
        // "tell application" would open a player that just quit.
        guard isRunning(p), let r = run(p.script), r.numberOfItems >= 7 else {
            if message["app"] as? String == p.name { clear() }
            return
        }
        let playing = r.atIndex(1)?.stringValue == "playing"
        // A player pausing must not hide another one that is still playing.
        if !playing, let current = message["app"] as? String, current != p.name, message["playing"] as? Bool == true {
            return
        }
        let title = r.atIndex(2)?.stringValue ?? ""
        let artist = r.atIndex(3)?.stringValue ?? ""
        message = [
            "t": "now",
            "app": p.name,
            "title": SerialLink.ascii(title),
            "artist": SerialLink.ascii(artist),
            "album": SerialLink.ascii(r.atIndex(4)?.stringValue ?? ""),
            "playing": playing,
            "pos": r.atIndex(5)?.doubleValue ?? 0,
            "dur": r.atIndex(6)?.doubleValue ?? 0,
        ]
        onChange()

        let key = "\(p.name)|\(r.atIndex(7)?.stringValue ?? title + artist)"
        if key != trackKey {
            trackKey = key
            loadArt(p, key: key)
        }
    }

    private func clear() {
        message = ["t": "now", "app": ""]
        trackKey = ""
        onChange()
        setArt(nil, key: "")
    }

    private func loadArt(_ p: Player, key: String) {
        if p.name == "Music" {
            setArt(run(Self.musicArt)?.data, key: key)
        } else if let s = run(Self.spotifyArt)?.stringValue, let url = URL(string: s) {
            URLSession.shared.dataTask(with: url) { data, _, _ in
                DispatchQueue.main.async {
                    MainActor.assumeIsolated { self.setArt(data, key: key) }
                }
            }.resume()
        } else {
            setArt(nil, key: key)
        }
    }

    private func setArt(_ data: Data?, key: String) {
        guard key == trackKey else { return } // the track changed while loading
        artMessage = ["t": "art"]
        if let data, let jpg = Self.cover(data) { artMessage["jpg"] = jpg }
        onArt()
    }

    /// Center-cropped, 220x220 baseline JPEG in base64 (what the board's decoder expects).
    static func cover(_ data: Data) -> String? {
        guard let src = CGImageSourceCreateWithData(data as CFData, nil),
              let img = CGImageSourceCreateImageAtIndex(src, 0, nil),
              let ctx = CGContext(data: nil, width: artSide, height: artSide, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return nil }
        let w = CGFloat(img.width), h = CGFloat(img.height), s = CGFloat(artSide)
        let scale = max(s / w, s / h)
        ctx.interpolationQuality = .high
        ctx.draw(img, in: CGRect(x: (s - w * scale) / 2, y: (s - h * scale) / 2, width: w * scale, height: h * scale))
        guard let out = ctx.makeImage() else { return nil }
        let rep = NSBitmapImageRep(cgImage: out)
        return rep.representation(using: .jpeg, properties: [.compressionFactor: 0.6])?.base64EncodedString()
    }
}

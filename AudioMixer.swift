import AppKit
import CoreAudio

/// Manages macOS audio output volume, media app volume (Spotify/Music), mic volume,
/// and output device switching for the "Mixer" page on the display.
///
/// Board → Mac:
///   {"t":"press","id":"vol:master:<0..100>"}
///   {"t":"press","id":"vol:media:<0..100>"}
///   {"t":"press","id":"vol:mic:<0..100>"}
///   {"t":"press","id":"mute:master"}
///   {"t":"press","id":"mute:media"}
///   {"t":"press","id":"mute:mic"}
///   {"t":"press","id":"outdev:<deviceID>"}
///
/// Mac → Board:
///   {"t":"audio","master":{"vol":75,"muted":false},"media":{"app":"Spotify","vol":60,"muted":false},
///    "mic":{"vol":80,"muted":false},"activeDev":"AirPods Pro","devices":[{"id":"12","name":"Speakers","active":false},...]}
@MainActor
final class AudioMixer {
    struct DeviceItem: Identifiable, Equatable {
        let id: AudioObjectID
        let name: String
        let active: Bool
    }

    var onChange: () -> Void = {}
    private(set) var masterVol: Int = 50
    private(set) var masterMuted: Bool = false
    private(set) var mediaApp: String = ""
    private(set) var mediaVol: Int = 50
    private(set) var mediaMuted: Bool = false
    private(set) var activeDeviceName: String = ""
    private(set) var devices: [DeviceItem] = []

    private let mic: MicControl
    private let nowPlaying: NowPlaying
    private var outputDevice = AudioObjectID(kAudioObjectUnknown)
    private var listeners: [(AudioObjectID, AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []
    private var isPageActive = false
    private var pollTimer: Timer?
    private var savedMasterVol: Float32 = 0.5

    init(mic: MicControl, nowPlaying: NowPlaying) {
        self.mic = mic
        self.nowPlaying = nowPlaying
    }

    var message: [String: Any] {
        [
            "t": "audio",
            "master": ["vol": masterVol, "muted": masterMuted],
            "media": ["app": SerialLink.ascii(mediaApp), "vol": mediaVol, "muted": mediaMuted],
            "mic": ["vol": mic.volumePct, "muted": mic.muted],
            "activeDev": SerialLink.ascii(activeDeviceName),
            "devices": devices.map {
                [
                    "id": "\($0.id)",
                    "name": SerialLink.ascii($0.name),
                    "active": $0.active
                ] as [String: Any]
            }
        ]
    }

    static func address(_ sel: AudioObjectPropertySelector,
                        _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
                        _ element: AudioObjectPropertyElement = kAudioObjectPropertyElementMain) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: element)
    }

    static let muteAddress = address(kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput)
    static let volumeAddresses = [0, 1, 2].map {
        address(kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput, AudioObjectPropertyElement($0))
    }
    static let ignoredDevicePrefixes = ["PMG", "LC34"]

    private func shouldIgnoreDevice(_ name: String) -> Bool {
        let upper = name.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        return Self.ignoredDevicePrefixes.contains { upper.hasPrefix($0.uppercased()) }
    }

    func start() {
        var defaultAddr = Self.address(kAudioHardwarePropertyDefaultOutputDevice)
        AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &defaultAddr, .main) { [weak self] _, _ in
            MainActor.assumeIsolated { self?.bindOutputDevice() }
        }

        var devicesAddr = Self.address(kAudioHardwarePropertyDevices)
        AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &devicesAddr, .main) { [weak self] _, _ in
            MainActor.assumeIsolated { self?.refreshDevices() }
        }

        bindOutputDevice()
    }

    func setActive(_ on: Bool) {
        isPageActive = on
        if on {
            refreshMediaVolume()
            if pollTimer == nil {
                let t = Timer(timeInterval: 3, repeats: true) { [weak self] _ in
                    MainActor.assumeIsolated {
                        self?.refreshMediaVolume()
                    }
                }
                RunLoop.main.add(t, forMode: .common)
                pollTimer = t
            }
        } else {
            pollTimer?.invalidate()
            pollTimer = nil
        }
    }

    private var scripts: [String: NSAppleScript] = [:]

    private func runScript(_ source: String) -> NSAppleEventDescriptor? {
        let script = scripts[source] ?? NSAppleScript(source: source)
        scripts[source] = script
        var error: NSDictionary?
        let result = script?.executeAndReturnError(&error)
        return error == nil ? result : nil
    }

    // MARK: - Master Output Controls

    func setMasterVolume(_ pct: Int) {
        let clamped = max(0, min(100, pct))
        let scalar = Float32(clamped) / 100.0
        let volumes = Self.volumeAddresses.filter { has($0) }
        for a in volumes { _ = set(a, scalar) }
        if clamped > 0 && masterMuted {
            _ = set(Self.muteAddress, UInt32(0))
        }
        readMaster()
    }

    func toggleMasterMute() {
        if set(Self.muteAddress, UInt32(masterMuted ? 0 : 1)) {
            readMaster()
            return
        }
        let volumes = Self.volumeAddresses.filter { has($0) }
        if masterMuted {
            for a in volumes { _ = set(a, savedMasterVol > 0.05 ? savedMasterVol : 0.5) }
        } else {
            if let v: Float32 = volumes.first.flatMap({ get($0, Float32(0)) }), v > 0 { savedMasterVol = v }
            for a in volumes { _ = set(a, Float32(0)) }
        }
        readMaster()
    }

    // MARK: - Device Switching

    func setDefaultOutputDevice(id: AudioObjectID) {
        var addr = Self.address(kAudioHardwarePropertyDefaultOutputDevice)
        var dev = id
        _ = AudioObjectSetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, UInt32(MemoryLayout<AudioObjectID>.size), &dev)
    }

    // MARK: - Media App Volume (Spotify / Music)

    func setMediaVolume(_ pct: Int) {
        let clamped = max(0, min(100, pct))
        mediaVol = clamped
        guard !mediaApp.isEmpty else { return }

        let script: String
        if mediaApp.lowercased().contains("spotify") {
            script = "tell application id \"com.spotify.client\" to set sound volume to \(clamped)"
        } else {
            script = "tell application id \"com.apple.Music\" to set sound volume to \(clamped)"
        }
        _ = runScript(script)
        onChange()
    }

    func toggleMediaMute() {
        mediaMuted.toggle()
        if mediaMuted {
            setMediaVolume(0)
        } else {
            setMediaVolume(mediaVol > 5 ? mediaVol : 50)
        }
    }

    func refreshMediaVolume() {
        let app = (nowPlaying.message["app"] as? String) ?? ""
        if app != mediaApp {
            mediaApp = app
        }
        guard !mediaApp.isEmpty else {
            mediaVol = 0
            onChange()
            return
        }

        let scriptSource = mediaApp.lowercased().contains("spotify")
            ? "tell application id \"com.spotify.client\" to get sound volume"
            : "tell application id \"com.apple.Music\" to get sound volume"

        if let desc = runScript(scriptSource) {
            let v = Int(desc.int32Value)
            if mediaVol != v {
                mediaVol = v
                onChange()
            }
        }
    }

    // MARK: - Press Actions

    func handleVolumePress(_ payload: String) {
        let parts = payload.split(separator: ":")
        guard parts.count >= 2, let val = Int(parts[1]) else { return }
        switch parts[0] {
        case "master":
            setMasterVolume(val)
        case "media":
            setMediaVolume(val)
        case "mic":
            mic.setVolume(val)
            onChange()
        default:
            break
        }
    }

    func handleMutePress(_ target: String) {
        switch target {
        case "master":
            toggleMasterMute()
        case "media":
            toggleMediaMute()
        case "mic":
            mic.toggle()
            onChange()
        default:
            break
        }
    }

    func handleOutDevPress(_ idStr: String) {
        guard let id = UInt32(idStr) else { return }
        setDefaultOutputDevice(id: AudioObjectID(id))
    }

    // MARK: - CoreAudio Internal Binding

    private func bindOutputDevice() {
        for (id, addr, block) in listeners {
            var a = addr
            AudioObjectRemovePropertyListenerBlock(id, &a, .main, block)
        }
        listeners = []

        var dev = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var addr = Self.address(kAudioHardwarePropertyDefaultOutputDevice)
        AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev)
        outputDevice = dev
        activeDeviceName = getDeviceName(dev)

        for a in [Self.muteAddress] + Self.volumeAddresses where has(a) {
            var a = a
            let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
                MainActor.assumeIsolated { self?.readMaster() }
            }
            if AudioObjectAddPropertyListenerBlock(dev, &a, .main, block) == noErr {
                listeners.append((dev, a, block))
            }
        }

        readMaster()
        refreshDevices()
    }

    private func readMaster() {
        let m: Bool
        if let v = get(Self.muteAddress, UInt32(0)), v != 0 {
            m = true
        } else if let a = Self.volumeAddresses.first(where: { has($0) }), let v = get(a, Float32(0)) {
            m = v < 0.01
        } else {
            m = false
        }

        var vol = 0
        if let a = Self.volumeAddresses.first(where: { has($0) }), let v = get(a, Float32(0)) {
            vol = Int((v * 100).rounded())
        }

        masterMuted = m
        masterVol = vol
        onChange()
    }

    private func refreshDevices() {
        var addr = Self.address(kAudioHardwarePropertyDevices)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr, size > 0 else { return }

        let count = Int(size) / MemoryLayout<AudioObjectID>.size
        var ids = [AudioObjectID](repeating: 0, count: count)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else { return }

        var list: [DeviceItem] = []
        for dev in ids where isOutputDevice(dev) {
            let name = getDeviceName(dev)
            if !name.isEmpty && !shouldIgnoreDevice(name) && !list.contains(where: { $0.name == name }) {
                list.append(DeviceItem(id: dev, name: name, active: dev == outputDevice))
            }
        }
        devices = list
        onChange()
    }

    private func getDeviceName(_ dev: AudioObjectID) -> String {
        var addr = Self.address(kAudioDevicePropertyDeviceNameCFString)
        var name: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        if AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &name) == noErr, let name {
            return name.takeRetainedValue() as String
        }
        return "Audio Device"
    }

    private func isOutputDevice(_ dev: AudioObjectID) -> Bool {
        var addr = Self.address(kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(dev, &addr, 0, nil, &size) == noErr, size > 0 else { return false }
        let rawBuffer = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { rawBuffer.deallocate() }
        let bufferList = rawBuffer.bindMemory(to: AudioBufferList.self, capacity: 1)
        guard AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, bufferList) == noErr else { return false }
        let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
        for b in buffers {
            if b.mNumberChannels > 0 { return true }
        }
        return false
    }

    private func has(_ addr: AudioObjectPropertyAddress) -> Bool {
        var a = addr
        return outputDevice != kAudioObjectUnknown && AudioObjectHasProperty(outputDevice, &a)
    }

    private func get<T: BitwiseCopyable>(_ addr: AudioObjectPropertyAddress, _ initial: T) -> T? {
        guard has(addr) else { return nil }
        var a = addr
        var v = initial
        var size = UInt32(MemoryLayout<T>.size)
        return AudioObjectGetPropertyData(outputDevice, &a, 0, nil, &size, &v) == noErr ? v : nil
    }

    private func set<T: BitwiseCopyable>(_ addr: AudioObjectPropertyAddress, _ value: T) -> Bool {
        guard has(addr) else { return false }
        var a = addr
        var settable: DarwinBoolean = false
        guard AudioObjectIsPropertySettable(outputDevice, &a, &settable) == noErr, settable.boolValue else { return false }
        var v = value
        return AudioObjectSetPropertyData(outputDevice, &a, 0, nil, UInt32(MemoryLayout<T>.size), &v) == noErr
    }
}

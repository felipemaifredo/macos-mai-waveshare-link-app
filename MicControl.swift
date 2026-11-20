import AppKit
import CoreAudio
import CoreMediaIO

/// Mutes the default input device and tells the board whether the mic or a camera is in use.
///   {"t":"mic","muted","micInUse","camInUse"}
/// Mic state comes from CoreAudio listeners (no polling). Cameras have no cheap system-wide
/// listener, so they are checked every 3 s, and only while the board is connected.
@MainActor
final class MicControl {
    var onChange: () -> Void = {}
    private(set) var muted = false
    private(set) var micInUse = false
    private(set) var camInUse = false

    private var device = AudioObjectID(kAudioObjectUnknown)
    private var listeners: [(AudioObjectID, AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []
    private var savedVolume: Float32 = 0.75
    private var cameraTimer: Timer?

    var message: [String: Any] { ["t": "mic", "muted": muted, "micInUse": micInUse, "camInUse": camInUse] }

    static func address(_ sel: AudioObjectPropertySelector,
                        _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
                        _ element: AudioObjectPropertyElement = kAudioObjectPropertyElementMain) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: element)
    }

    static let muteAddress = address(kAudioDevicePropertyMute, kAudioDevicePropertyScopeInput)
    static let runningAddress = address(kAudioDevicePropertyDeviceIsRunningSomewhere)
    // Some devices only have per-channel volume.
    static let volumeAddresses = [0, 1, 2].map {
        address(kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeInput, AudioObjectPropertyElement($0))
    }

    func start() {
        var addr = Self.address(kAudioHardwarePropertyDefaultInputDevice)
        AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &addr, .main) { [weak self] _, _ in
            MainActor.assumeIsolated { self?.bind() }
        }
        bind()
    }

    func setActive(_ on: Bool) {
        if on, cameraTimer == nil {
            let t = Timer(timeInterval: 3, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.checkCameras() }
            }
            RunLoop.main.add(t, forMode: .common)
            cameraTimer = t
            checkCameras()
        } else if !on {
            cameraTimer?.invalidate()
            cameraTimer = nil
        }
    }

    func toggle() {
        if set(Self.muteAddress, UInt32(muted ? 0 : 1)) {
            read()
            return
        }
        let volumes = Self.volumeAddresses.filter { has($0) }
        if muted {
            for a in volumes { _ = set(a, savedVolume > 0.05 ? savedVolume : 0.75) }
        } else {
            if let v: Float32 = volumes.first.flatMap({ get($0, Float32(0)) }), v > 0 { savedVolume = v }
            for a in volumes { _ = set(a, Float32(0)) }
        }
        read()
    }

    var volumePct: Int {
        let volumes = Self.volumeAddresses.filter { has($0) }
        if let v: Float32 = volumes.first.flatMap({ get($0, Float32(0)) }) {
            return Int((v * 100).rounded())
        }
        return muted ? 0 : 75
    }

    func setVolume(_ pct: Int) {
        let v = Float32(max(0, min(100, pct))) / 100.0
        let volumes = Self.volumeAddresses.filter { has($0) }
        for a in volumes { _ = set(a, v) }
        if pct > 0 && muted {
            _ = set(Self.muteAddress, UInt32(0))
        }
        read()
    }

    // MARK: Default input device

    private func bind() {
        for (id, addr, block) in listeners {
            var a = addr
            AudioObjectRemovePropertyListenerBlock(id, &a, .main, block)
        }
        listeners = []

        var dev = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var addr = Self.address(kAudioHardwarePropertyDefaultInputDevice)
        AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev)
        device = dev

        for a in [Self.muteAddress, Self.runningAddress] + Self.volumeAddresses where has(a) {
            var a = a
            let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
                MainActor.assumeIsolated { self?.read() }
            }
            if AudioObjectAddPropertyListenerBlock(dev, &a, .main, block) == noErr {
                listeners.append((dev, a, block))
            }
        }
        read()
    }

    private func read() {
        let m: Bool
        if let v = get(Self.muteAddress, UInt32(0)), v != 0 {
            m = true
        } else if let a = Self.volumeAddresses.first(where: { has($0) }), let v = get(a, Float32(0)) {
            m = v < 0.01
        } else {
            m = false
        }
        let inUse = (get(Self.runningAddress, UInt32(0)) ?? 0) != 0
        guard m != muted || inUse != micInUse else { return }
        muted = m
        micInUse = inUse
        onChange()
    }

    private func has(_ addr: AudioObjectPropertyAddress) -> Bool {
        var a = addr
        return device != kAudioObjectUnknown && AudioObjectHasProperty(device, &a)
    }

    private func get<T: BitwiseCopyable>(_ addr: AudioObjectPropertyAddress, _ initial: T) -> T? {
        guard has(addr) else { return nil }
        var a = addr
        var v = initial
        var size = UInt32(MemoryLayout<T>.size)
        return AudioObjectGetPropertyData(device, &a, 0, nil, &size, &v) == noErr ? v : nil
    }

    private func set<T: BitwiseCopyable>(_ addr: AudioObjectPropertyAddress, _ value: T) -> Bool {
        guard has(addr) else { return false }
        var a = addr
        var settable: DarwinBoolean = false
        guard AudioObjectIsPropertySettable(device, &a, &settable) == noErr, settable.boolValue else { return false }
        var v = value
        return AudioObjectSetPropertyData(device, &a, 0, nil, UInt32(MemoryLayout<T>.size), &v) == noErr
    }

    // MARK: Cameras

    private func checkCameras() {
        let system = CMIOObjectID(kCMIOObjectSystemObject)
        var addr = CMIOObjectPropertyAddress(
            mSelector: CMIOObjectPropertySelector(kCMIOHardwarePropertyDevices),
            mScope: CMIOObjectPropertyScope(kCMIOObjectPropertyScopeGlobal),
            mElement: CMIOObjectPropertyElement(kCMIOObjectPropertyElementMain))
        var size: UInt32 = 0
        guard CMIOObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == 0, size > 0 else { return }
        var ids = [CMIOObjectID](repeating: 0, count: Int(size) / MemoryLayout<CMIOObjectID>.size)
        var used: UInt32 = 0
        guard CMIOObjectGetPropertyData(system, &addr, 0, nil, size, &used, &ids) == 0 else { return }

        var running = false
        for id in ids {
            var a = CMIOObjectPropertyAddress(
                mSelector: CMIOObjectPropertySelector(kCMIODevicePropertyDeviceIsRunningSomewhere),
                mScope: CMIOObjectPropertyScope(kCMIOObjectPropertyScopeGlobal),
                mElement: CMIOObjectPropertyElement(kCMIOObjectPropertyElementMain))
            var v: UInt32 = 0
            var got: UInt32 = 0
            if CMIOObjectGetPropertyData(id, &a, 0, nil, UInt32(MemoryLayout<UInt32>.size), &got, &v) == 0, v != 0 {
                running = true
            }
        }
        if running != camInUse {
            camInUse = running
            onChange()
        }
    }
}

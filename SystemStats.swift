import AppKit
import IOKit.ps

/// CPU, memory, battery and network for the "Mac" page, every 2 s while that page is on screen.
///   {"t":"mac","cpu","mem","memUsed","memTotal","bat","charging","ac","rx","tx"}
/// Each sample is a handful of system calls (well under 1 ms), and nothing runs while the page is hidden.
@MainActor
final class SystemStats {
    var onSample: ([String: Any]) -> Void = { _ in }
    private var timer: Timer?
    private var lastTicks: [UInt32]?
    private var lastNet: [String: (rx: UInt32, tx: UInt32)] = [:]
    private var lastNetAt = Date()

    func setActive(_ on: Bool) {
        if on, timer == nil {
            lastTicks = cpuTicks()
            _ = network() // baseline for the rates
            let t = Timer(timeInterval: 2, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.sample() }
            }
            RunLoop.main.add(t, forMode: .common)
            timer = t
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                MainActor.assumeIsolated { if self?.timer != nil { self?.sample() } }
            }
        } else if !on {
            timer?.invalidate()
            timer = nil
        }
    }

    private func sample() {
        var msg: [String: Any] = ["t": "mac"]
        if let cpu = cpuPercent() { msg["cpu"] = cpu }
        let (used, total) = memory()
        msg["memUsed"] = (used * 10).rounded() / 10
        msg["memTotal"] = total.rounded()
        msg["mem"] = total > 0 ? Int((used / total * 100).rounded()) : -1
        let b = battery()
        msg["bat"] = b.pct
        msg["charging"] = b.charging
        msg["ac"] = b.ac
        let n = network()
        msg["rx"] = Int(n.rx)
        msg["tx"] = Int(n.tx)
        onSample(msg)
    }

    // MARK: CPU

    private func cpuTicks() -> [UInt32]? {
        var info = host_cpu_load_info()
        var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info_data_t>.size / MemoryLayout<integer_t>.size)
        let r = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, $0, &count)
            }
        }
        guard r == KERN_SUCCESS else { return nil }
        let t = info.cpu_ticks
        return [t.0, t.1, t.2, t.3] // user, system, idle, nice
    }

    private func cpuPercent() -> Int? {
        guard let now = cpuTicks(), let last = lastTicks else { return nil }
        lastTicks = now
        let d = zip(now, last).map { Double($0 &- $1) }
        let total = d.reduce(0, +)
        guard total > 0 else { return nil }
        return Int(((total - d[2]) / total * 100).rounded())
    }

    // MARK: Memory (same "Memory Used" as Activity Monitor: app + wired + compressed)

    private func memory() -> (used: Double, total: Double) {
        let total = Double(ProcessInfo.processInfo.physicalMemory) / 1_073_741_824
        var vm = vm_statistics64()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let r = withUnsafeMutablePointer(to: &vm) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        guard r == KERN_SUCCESS else { return (0, total) }
        let app = UInt64(vm.internal_page_count) - UInt64(vm.purgeable_count)
        let pages = app + UInt64(vm.wire_count) + UInt64(vm.compressor_page_count)
        return (Double(pages * UInt64(vm_kernel_page_size)) / 1_073_741_824, total)
    }

    // MARK: Battery

    private func battery() -> (pct: Int, charging: Bool, ac: Bool) {
        guard let info = IOPSCopyPowerSourcesInfo()?.takeRetainedValue(),
              let list = IOPSCopyPowerSourcesList(info)?.takeRetainedValue() as? [CFTypeRef] else { return (-1, false, true) }
        for ps in list {
            guard let d = IOPSGetPowerSourceDescription(info, ps)?.takeUnretainedValue() as? [String: Any],
                  d[kIOPSTypeKey] as? String == kIOPSInternalBatteryType,
                  let cur = d[kIOPSCurrentCapacityKey] as? Int,
                  let max = d[kIOPSMaxCapacityKey] as? Int, max > 0 else { continue }
            return (cur * 100 / max,
                    d[kIOPSIsChargingKey] as? Bool ?? false,
                    d[kIOPSPowerSourceStateKey] as? String == kIOPSACPowerValue)
        }
        return (-1, false, true)
    }

    // MARK: Network (bytes/s over the en* interfaces)

    private func network() -> (rx: Double, tx: Double) {
        var ifap: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&ifap) == 0, let first = ifap else { return (0, 0) }
        defer { freeifaddrs(ifap) }

        var now: [String: (rx: UInt32, tx: UInt32)] = [:]
        for p in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let a = p.pointee
            guard let addr = a.ifa_addr, addr.pointee.sa_family == UInt8(AF_LINK), let data = a.ifa_data else { continue }
            let name = String(cString: a.ifa_name)
            guard name.hasPrefix("en") else { continue }
            let d = data.assumingMemoryBound(to: if_data.self).pointee
            now[name] = (d.ifi_ibytes, d.ifi_obytes)
        }

        let at = Date()
        let dt = max(at.timeIntervalSince(lastNetAt), 0.1)
        var rx = 0.0, tx = 0.0
        for (name, n) in now {
            guard let l = lastNet[name] else { continue }
            rx += Double(n.rx &- l.rx) // the counters are 32-bit and wrap
            tx += Double(n.tx &- l.tx)
        }
        lastNet = now
        lastNetAt = at
        return (rx / dt, tx / dt)
    }
}

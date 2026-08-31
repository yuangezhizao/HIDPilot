import CoreGraphics
import Foundation
import IOKit.hid

private let frameSize = 64
private let vendorID = 0xcafe
private let productID = 0x4008
private let usagePage = 0xff00
private let usage = 1

private enum Command: UInt8 {
    case status = 0x01
    case configInfo = 0x02
    case configChunk = 0x03
    case stageBegin = 0x10
    case stageChunk = 0x11
    case stageValidate = 0x12
    case runOnce = 0x13
    case applyTemporary = 0x14
    case applySave = 0x15
    case restoreDefault = 0x16
    case rebootApplication = 0x17
    case rebootBootsel = 0x18
}

private func readUInt16(_ data: [UInt8], _ offset: Int) -> UInt16 {
    UInt16(data[offset]) | UInt16(data[offset + 1]) << 8
}

private func readUInt32(_ data: [UInt8], _ offset: Int) -> UInt32 {
    UInt32(data[offset]) | UInt32(data[offset + 1]) << 8 | UInt32(data[offset + 2]) << 16 | UInt32(data[offset + 3]) << 24
}

private func appendUInt16(_ value: UInt16, to data: inout [UInt8]) {
    data.append(UInt8(truncatingIfNeeded: value))
    data.append(UInt8(truncatingIfNeeded: value >> 8))
}

private func appendUInt32(_ value: UInt32, to data: inout [UInt8]) {
    data.append(UInt8(truncatingIfNeeded: value))
    data.append(UInt8(truncatingIfNeeded: value >> 8))
    data.append(UInt8(truncatingIfNeeded: value >> 16))
    data.append(UInt8(truncatingIfNeeded: value >> 24))
}

private func crc32(_ data: [UInt8]) -> UInt32 {
    var crc = UInt32.max
    for byte in data {
        crc ^= UInt32(byte)
        for _ in 0..<8 {
            crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : 0xedb88320)
        }
    }
    return ~crc
}

private func defaultConfig() -> [UInt8] {
    var data = [UInt8](repeating: 0, count: 36)
    data[0] = 1
    data[1] = 1
    data[2] = 3
    data[4] = 0xd8
    data[5] = 0xd6
    data[12] = 2
    data[13] = 100
    data[20] = 1
    data[24] = 200
    data[28] = 2
    data[29] = UInt8(bitPattern: -100)
    return data
}

private func harmlessPersistentConfig() -> [UInt8] {
    var data = defaultConfig()
    data[4] = 0xe8
    data[5] = 0xfd
    data[13] = 20
    data[29] = UInt8(bitPattern: -20)
    return data
}

private func pacedConfig() -> [UInt8] {
    var data = [UInt8](repeating: 0, count: 28)
    data[0] = 1
    data[2] = 2
    data[4] = 0x88
    data[5] = 0x13
    data[12] = 2
    data[13] = 40
    data[17] = 0xd0
    data[18] = 0x07
    data[20] = 2
    data[21] = UInt8(bitPattern: -40)
    data[25] = 0xd0
    data[26] = 0x07
    return data
}

private final class HIDProbe: @unchecked Sendable {
    private let manager: IOHIDManager
    private let device: IOHIDDevice
    private let inputBuffer: UnsafeMutablePointer<UInt8>
    private var response: [UInt8]?
    private var asynchronousWriteResult: IOReturn?
    private var transaction: UInt16 = 0

    private static let inputCallback: IOHIDReportCallback = { context, result, _, _, _, report, length in
        guard result == kIOReturnSuccess, let context else { return }
        let probe = Unmanaged<HIDProbe>.fromOpaque(context).takeUnretainedValue()
        probe.response = Array(UnsafeBufferPointer(start: report, count: length))
    }

    init(timeout: TimeInterval = 5) throws {
        manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let matching: [String: Any] = [
            kIOHIDVendorIDKey as String: vendorID,
            kIOHIDProductIDKey as String: productID,
            kIOHIDPrimaryUsagePageKey as String: usagePage,
            kIOHIDPrimaryUsageKey as String: usage,
        ]
        IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)
        guard IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone)) == kIOReturnSuccess else {
            throw ProbeError("无法打开 IOHIDManager")
        }
        let deadline = Date().addingTimeInterval(timeout)
        var found: IOHIDDevice?
        repeat {
            found = (IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>)?.first
            if found == nil { Thread.sleep(forTimeInterval: 0.05) }
        } while found == nil && Date() < deadline
        guard let found else { throw ProbeError("未找到 HIDPilot 厂商 HID 集合") }
        device = found
        guard IOHIDDeviceOpen(device, IOOptionBits(kIOHIDOptionsTypeNone)) == kIOReturnSuccess else {
            throw ProbeError("无法打开 HIDPilot 厂商 HID 集合")
        }
        inputBuffer = .allocate(capacity: frameSize)
        inputBuffer.initialize(repeating: 0, count: frameSize)
        IOHIDDeviceRegisterInputReportCallback(device, inputBuffer, frameSize, Self.inputCallback, Unmanaged.passUnretained(self).toOpaque())
        IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
    }

    deinit {
        IOHIDDeviceUnscheduleFromRunLoop(device, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
        IOHIDDeviceClose(device, IOOptionBits(kIOHIDOptionsTypeNone))
        IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        inputBuffer.deinitialize(count: frameSize)
        inputBuffer.deallocate()
    }

    private func nextTransaction() -> UInt16 {
        transaction &+= 1
        if transaction == 0 { transaction = 1 }
        return transaction
    }

    private func makeFrame(_ command: Command, transaction: UInt16, payload: [UInt8]) -> [UInt8] {
        precondition(payload.count <= 56)
        var frame = [UInt8](repeating: 0, count: frameSize)
        frame[0] = 0x48
        frame[1] = 0x50
        frame[2] = 1
        frame[3] = command.rawValue
        frame[4] = UInt8(truncatingIfNeeded: transaction)
        frame[5] = UInt8(truncatingIfNeeded: transaction >> 8)
        frame[6] = UInt8(payload.count)
        frame.replaceSubrange(8..<(8 + payload.count), with: payload)
        return frame
    }

    func beginRequest(_ command: Command, payload: [UInt8] = [], transaction suppliedTransaction: UInt16? = nil) throws -> UInt16 {
        let requestTransaction = suppliedTransaction ?? nextTransaction()
        var frame = makeFrame(command, transaction: requestTransaction, payload: payload)
        response = nil
        let writeResult = frame.withUnsafeMutableBufferPointer {
            IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, $0.baseAddress!, frameSize)
        }
        guard writeResult == kIOReturnSuccess else { throw ProbeError("Output Report 写入失败：\(writeResult)") }
        return requestTransaction
    }

    func beginRequestAsynchronously(_ command: Command, payload: [UInt8] = [], transaction suppliedTransaction: UInt16? = nil) -> UInt16 {
        let requestTransaction = suppliedTransaction ?? nextTransaction()
        let requestFrame = makeFrame(command, transaction: requestTransaction, payload: payload)
        response = nil
        asynchronousWriteResult = nil
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            Thread.sleep(forTimeInterval: 0.05)
            var frame = requestFrame
            asynchronousWriteResult = frame.withUnsafeMutableBufferPointer {
                IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, $0.baseAddress!, frameSize)
            }
        }
        return requestTransaction
    }

    func finishRequest(_ command: Command, transaction requestTransaction: UInt16, timeout: TimeInterval = 2) throws -> [UInt8] {
        let deadline = Date().addingTimeInterval(timeout)
        while response == nil && Date() < deadline {
            CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.01, true)
        }
        guard let response, response.count == frameSize else { throw ProbeError("等待 Input Report 超时") }
        guard response[0] == 0x48, response[1] == 0x50, response[2] == 1,
              response[3] == command.rawValue | 0x80, readUInt16(response, 4) == requestTransaction else {
            throw ProbeError("响应帧与请求不匹配")
        }
        guard response[7] == 0 else { throw ProbeError("设备返回状态码 \(response[7])") }
        return Array(response[8..<(8 + Int(response[6]))])
    }

    func request(_ command: Command, payload: [UInt8] = [], transaction suppliedTransaction: UInt16? = nil, timeout: TimeInterval = 2) throws -> [UInt8] {
        let requestTransaction = try beginRequest(command, payload: payload, transaction: suppliedTransaction)
        return try finishRequest(command, transaction: requestTransaction, timeout: timeout)
    }

    func prepareStage(_ config: [UInt8]) throws -> UInt16 {
        let stageTransaction = nextTransaction()
        var begin: [UInt8] = []
        appendUInt16(UInt16(config.count), to: &begin)
        appendUInt32(crc32(config), to: &begin)
        _ = try request(.stageBegin, payload: begin, transaction: stageTransaction)
        var offset = 0
        while offset < config.count {
            let count = min(54, config.count - offset)
            var chunk: [UInt8] = []
            appendUInt16(UInt16(offset), to: &chunk)
            chunk.append(contentsOf: config[offset..<(offset + count)])
            _ = try request(.stageChunk, payload: chunk, transaction: stageTransaction)
            offset += count
        }
        _ = try request(.stageValidate, transaction: stageTransaction)
        return stageTransaction
    }

    func stage(_ config: [UInt8], finalCommand: Command) throws {
        let stageTransaction = try prepareStage(config)
        _ = try request(finalCommand, transaction: stageTransaction)
    }

    func readConfig() throws -> [UInt8] {
        let info = try request(.configInfo)
        let length = Int(readUInt16(info, 0))
        let expectedCRC = readUInt32(info, 2)
        var config = [UInt8](repeating: 0, count: length)
        var offset = 0
        while offset < length {
            let count = min(54, length - offset)
            var payload: [UInt8] = []
            appendUInt16(UInt16(offset), to: &payload)
            payload.append(UInt8(count))
            let chunk = try request(.configChunk, payload: payload)
            guard Int(readUInt16(chunk, 0)) == offset else { throw ProbeError("配置分块偏移错误") }
            config.replaceSubrange(offset..<(offset + count), with: chunk[2...])
            offset += count
        }
        guard crc32(config) == expectedCRC else { throw ProbeError("活动配置 CRC 不匹配") }
        return config
    }
}

private struct ProbeError: Error, CustomStringConvertible {
    let description: String
    init(_ description: String) { self.description = description }
}

private func statusSummary(_ payload: [UInt8]) -> String {
    let firmware = "\(payload[0]).\(payload[1]).\(payload[2])"
    let enabled = payload[4] & 1 != 0
    return "firmware=\(firmware) schema=\(payload[3]) enabled=\(enabled) actions=\(payload[6]) interval_ms=\(readUInt32(payload, 8)) runs=\(readUInt32(payload, 12)) errors=\(readUInt32(payload, 16)) generation=\(readUInt32(payload, 20))"
}

private func waitForReboot(timeout: TimeInterval = 8) throws -> HIDProbe {
    let deadline = Date().addingTimeInterval(timeout)
    var observedDisconnect = false
    repeat {
        if observedDisconnect {
            if let probe = try? HIDProbe(timeout: 0.2) { return probe }
        } else if (try? HIDProbe(timeout: 0.05)) == nil {
            observedDisconnect = true
        }
        Thread.sleep(forTimeInterval: 0.1)
    } while Date() < deadline
    throw ProbeError(observedDisconnect ? "设备断开后未重新枚举" : "未观察到设备执行 USB 重启")
}

private func sampleRunOnce(_ probe: HIDProbe, config: [UInt8]) throws {
    let displayBounds = CGDisplayBounds(CGMainDisplayID())
    let center = CGPoint(x: displayBounds.midX, y: displayBounds.midY)
    CGWarpMouseCursorPosition(center)
    Thread.sleep(forTimeInterval: 1.0)
    let baseline = CGEvent(source: nil)?.location ?? center
    let stageTransaction = try probe.prepareStage(config)
    let start = Date()
    let requestTransaction = probe.beginRequestAsynchronously(.runOnce, transaction: stageTransaction)
    var minimumX = baseline.x
    var maximumX = baseline.x
    var minimumAt: TimeInterval = 0
    var maximumAt: TimeInterval = 0
    var departedAt: TimeInterval?
    var returnedAt: TimeInterval?
    while Date().timeIntervalSince(start) < 0.6 {
        let elapsed = Date().timeIntervalSince(start)
        let location = CGEvent(source: nil)?.location ?? baseline
        if location.x < minimumX { minimumX = location.x; minimumAt = elapsed }
        if location.x > maximumX { maximumX = location.x; maximumAt = elapsed }
        if departedAt == nil && location.x > baseline.x + 5 { departedAt = elapsed }
        if departedAt != nil && returnedAt == nil && abs(location.x - baseline.x) < 5 { returnedAt = elapsed }
        CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.001, true)
    }
    _ = try probe.finishRequest(.runOnce, transaction: requestTransaction)
    let final = CGEvent(source: nil)?.location ?? baseline
    let holdMilliseconds = ((returnedAt ?? 0) - (departedAt ?? 0)) * 1000
    print(String(format: "run_once baseline_x=%.1f min_x=%.1f min_ms=%.1f max_x=%.1f max_ms=%.1f delta_x=%.1f hold_ms=%.1f final_x=%.1f", baseline.x, minimumX, minimumAt * 1000, maximumX, maximumAt * 1000, maximumX - baseline.x, holdMilliseconds, final.x))
}

private func samplePacedRunOnce(_ probe: HIDProbe) throws {
    let displayBounds = CGDisplayBounds(CGMainDisplayID())
    let center = CGPoint(x: displayBounds.midX, y: displayBounds.midY)
    CGWarpMouseCursorPosition(center)
    Thread.sleep(forTimeInterval: 1.0)
    let baseline = CGEvent(source: nil)?.location ?? center
    let stageTransaction = try probe.prepareStage(pacedConfig())
    let start = Date()
    let requestTransaction = probe.beginRequestAsynchronously(.runOnce, transaction: stageTransaction)
    var maximumX = baseline.x
    var maximumAt: TimeInterval = 0
    var lastX = baseline.x
    var lastChangeAt: TimeInterval = 0
    while Date().timeIntervalSince(start) < 5.0 {
        let elapsed = Date().timeIntervalSince(start)
        let location = CGEvent(source: nil)?.location ?? baseline
        if location.x > maximumX { maximumX = location.x; maximumAt = elapsed }
        if abs(location.x - lastX) > 0.01 { lastChangeAt = elapsed; lastX = location.x }
        CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.001, true)
    }
    _ = try probe.finishRequest(.runOnce, transaction: requestTransaction)
    let final = CGEvent(source: nil)?.location ?? baseline
    CGWarpMouseCursorPosition(center)
    print(String(format: "paced_run baseline_x=%.1f max_x=%.1f max_at_ms=%.1f last_change_ms=%.1f final_x=%.1f", baseline.x, maximumX, maximumAt * 1000, lastChangeAt * 1000, final.x))
    guard maximumX > baseline.x + 5 else { throw ProbeError("缓速动作未产生可观测正向位移") }
    guard maximumAt > 1.5, maximumAt < 2.6 else { throw ProbeError("正向缓速移动时长不在预期范围") }
    guard lastChangeAt > 3.5, lastChangeAt < 4.8 else { throw ProbeError("反向缓速移动时长不在预期范围") }
    guard abs(final.x - baseline.x) < 5 else { throw ProbeError("缓速往返动作结束后未回到起点") }
}

private func runAcceptance() throws {
    var probe: HIDProbe? = try HIDProbe()
    print("initial \(statusSummary(try probe!.request(.status)))")
    let active = try probe!.readConfig()
    guard active == defaultConfig() else { throw ProbeError("初始配置不是默认配置") }
    try sampleRunOnce(probe!, config: active)

    var paused = harmlessPersistentConfig()
    paused[1] = 0
    try probe!.stage(paused, finalCommand: .applySave)
    print("paused \(statusSummary(try probe!.request(.status)))")
    _ = try probe!.request(.rebootApplication)
    probe = nil
    probe = try waitForReboot()
    guard try probe!.readConfig() == paused else { throw ProbeError("暂停配置重启后未保持") }
    print("paused_reboot \(statusSummary(try probe!.request(.status)))")

    let harmless = harmlessPersistentConfig()
    try probe!.stage(harmless, finalCommand: .applyTemporary)
    print("temporary \(statusSummary(try probe!.request(.status)))")
    try probe!.stage(harmless, finalCommand: .applySave)
    _ = try probe!.request(.rebootApplication)
    probe = nil
    probe = try waitForReboot()
    guard try probe!.readConfig() == harmless else { throw ProbeError("无害配置重启后未保持") }
    print("saved_reboot \(statusSummary(try probe!.request(.status)))")

    _ = try probe!.request(.restoreDefault)
    _ = try probe!.request(.rebootApplication)
    probe = nil
    probe = try waitForReboot()
    guard try probe!.readConfig() == defaultConfig() else { throw ProbeError("默认配置恢复后未保持") }
    let finalStatus = try probe!.request(.status)
    guard finalStatus[4] & 1 != 0 else { throw ProbeError("最终配置未启用") }
    print("final \(statusSummary(finalStatus))")
}

private func verifyPacedPersistence() throws {
    var probe: HIDProbe? = try HIDProbe()
    let paced = pacedConfig()
    try probe!.stage(paced, finalCommand: .applySave)
    _ = try probe!.request(.rebootApplication)
    probe = nil
    probe = try waitForReboot()
    guard try probe!.readConfig() == paced else { throw ProbeError("移动时长配置重启后未保持") }
    print("paced_persisted \(statusSummary(try probe!.request(.status)))")
    _ = try probe!.request(.restoreDefault)
    _ = try probe!.request(.rebootApplication)
    probe = nil
    probe = try waitForReboot()
    guard try probe!.readConfig() == defaultConfig() else { throw ProbeError("持久化验收后未恢复默认配置") }
    print("default_restored \(statusSummary(try probe!.request(.status)))")
}

private func monitorCycles(_ probe: HIDProbe) throws {
    let initialStatus = try probe.request(.status)
    var previousRuns = readUInt32(initialStatus, 12)
    let start = Date()
    var completions: [TimeInterval] = []
    while Date().timeIntervalSince(start) < 120, completions.count < 2 {
        let status = try probe.request(.status)
        let runs = readUInt32(status, 12)
        if runs != previousRuns {
            let elapsed = Date().timeIntervalSince(start)
            print(String(format: "cycle_completion run=%u after_s=%.3f", runs, elapsed))
            completions.append(elapsed)
            previousRuns = runs
        }
        CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.2, true)
    }
    guard completions.count == 2 else { throw ProbeError("120 秒内未观察到两个周期") }
    print(String(format: "cycle_interval_s=%.3f", completions[1] - completions[0]))
}

do {
    let mode = CommandLine.arguments.dropFirst().first
    if mode == "acceptance" {
        try runAcceptance()
    } else if mode == "trial" {
        let probe = try HIDProbe()
        let config = try probe.readConfig()
        try sampleRunOnce(probe, config: config)
        print(statusSummary(try probe.request(.status)))
    } else if mode == "monitor" {
        let probe = try HIDProbe()
        try monitorCycles(probe)
    } else if mode == "paced" {
        let probe = try HIDProbe()
        try samplePacedRunOnce(probe)
        print(statusSummary(try probe.request(.status)))
    } else if mode == "paced-persist" {
        try verifyPacedPersistence()
    } else if mode == "bootsel" {
        let probe = try HIDProbe()
        _ = try probe.request(.rebootBootsel)
        print("bootsel_requested")
    } else {
        let probe = try HIDProbe()
        print(statusSummary(try probe.request(.status)))
        print("config_crc=\(String(format: "%08x", crc32(try probe.readConfig())))")
    }
} catch {
    fputs("hardware probe failed: \(error)\n", stderr)
    exit(1)
}

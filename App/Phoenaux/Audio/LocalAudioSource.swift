import AVFAudio
import Foundation

enum LocalAudioSourceError: LocalizedError {
    case emptyFile
    case unsupportedChannelCount(Int)
    case decodedAudioTooLarge
    case bufferAllocationFailed
    case sourceAllocationFailed
    case sourceAppendFailed
    case sourceSealFailed

    var errorDescription: String? {
        switch self {
        case .emptyFile: "The selected audio file contains no playable frames."
        case let .unsupportedChannelCount(count):
            "Phoenaux currently supports mono and stereo files, not \(count) channels."
        case .decodedAudioTooLarge:
            "This file exceeds Phoenaux’s 256 MiB decoded-audio limit."
        case .bufferAllocationFailed: "Phoenaux could not allocate a decode buffer."
        case .sourceAllocationFailed: "Phoenaux could not allocate its local audio source."
        case .sourceAppendFailed: "Phoenaux could not store the decoded audio safely."
        case .sourceSealFailed: "Phoenaux could not finalize the decoded audio source."
        }
    }
}

final class LocalAudioSource: @unchecked Sendable {
    let pointer: OpaquePointer
    let displayName: String
    let sampleRate: Double
    let channelCount: Int
    let frameCount: Int

    var duration: TimeInterval {
        sampleRate > 0 ? Double(frameCount) / sampleRate : 0
    }

    init(
        pointer: OpaquePointer,
        displayName: String,
        sampleRate: Double,
        channelCount: Int,
        frameCount: Int
    ) {
        self.pointer = pointer
        self.displayName = displayName
        self.sampleRate = sampleRate
        self.channelCount = channelCount
        self.frameCount = frameCount
    }

    deinit {
        PXPCMSourceDestroy(pointer)
    }
}

enum LocalAudioDecoder {
    private static let decodeChunkFrames: AVAudioFrameCount = 8_192
    private static let maximumDecodedBytes: Int64 = 256 * 1_024 * 1_024

    static func decode(_ url: URL) async throws -> LocalAudioSource {
        try await Task.detached(priority: .userInitiated) {
            let accessedSecurityScope = url.startAccessingSecurityScopedResource()
            defer {
                if accessedSecurityScope {
                    url.stopAccessingSecurityScopedResource()
                }
            }

            let file = try AVAudioFile(
                forReading: url,
                commonFormat: .pcmFormatFloat32,
                interleaved: false
            )
            let format = file.processingFormat
            let channelCount = Int(format.channelCount)
            guard (1...2).contains(channelCount) else {
                throw LocalAudioSourceError.unsupportedChannelCount(channelCount)
            }
            guard file.length > 0 else {
                throw LocalAudioSourceError.emptyFile
            }

            let (sampleCount, sampleOverflow) = Int64(file.length)
                .multipliedReportingOverflow(by: Int64(channelCount))
            let (decodedBytes, byteOverflow) = sampleCount.multipliedReportingOverflow(by: 4)
            guard !sampleOverflow, !byteOverflow, decodedBytes <= maximumDecodedBytes else {
                throw LocalAudioSourceError.decodedAudioTooLarge
            }
            guard let pointer = PXPCMSourceCreate(format.sampleRate, channelCount) else {
                throw LocalAudioSourceError.sourceAllocationFailed
            }
            var ownsPointer = true
            defer {
                if ownsPointer {
                    PXPCMSourceDestroy(pointer)
                }
            }
            guard let buffer = AVAudioPCMBuffer(
                pcmFormat: format,
                frameCapacity: decodeChunkFrames
            ) else {
                throw LocalAudioSourceError.bufferAllocationFailed
            }
            var channelPointers = [UnsafeMutablePointer<Float>?](
                repeating: nil,
                count: channelCount
            )

            while file.framePosition < file.length {
                let remaining = file.length - file.framePosition
                let requested = AVAudioFrameCount(min(
                    remaining,
                    AVAudioFramePosition(decodeChunkFrames)
                ))
                try file.read(into: buffer, frameCount: requested)
                guard buffer.frameLength > 0 else { break }
                guard let channels = buffer.floatChannelData else {
                    throw LocalAudioSourceError.sourceAppendFailed
                }
                for channel in 0..<channelCount {
                    channelPointers[channel] = channels[channel]
                }
                let appended = channelPointers.withUnsafeBufferPointer { pointers in
                    PXPCMSourceAppend(
                        pointer,
                        pointers.baseAddress,
                        channelCount,
                        Int(buffer.frameLength)
                    )
                }
                guard appended else { throw LocalAudioSourceError.sourceAppendFailed }
            }
            guard PXPCMSourceSeal(pointer) else {
                throw LocalAudioSourceError.sourceSealFailed
            }
            ownsPointer = false
            return LocalAudioSource(
                pointer: pointer,
                displayName: url.deletingPathExtension().lastPathComponent,
                sampleRate: format.sampleRate,
                channelCount: channelCount,
                frameCount: Int(PXPCMSourceFrameCount(pointer))
            )
        }.value
    }
}

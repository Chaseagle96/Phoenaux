import AVFAudio
import Foundation
import Observation

enum HostedAudioEngineError: LocalizedError {
    case engineAllocationFailed
    case invalidOutputFormat
    case dspPreparationFailed

    var errorDescription: String? {
        switch self {
        case .engineAllocationFailed: "PhoenauxDSP could not allocate its engine."
        case .invalidOutputFormat: "The current output route has no usable audio format."
        case .dspPreparationFailed: "PhoenauxDSP could not prepare for the current route."
        }
    }
}

enum HostedAudioSourceKind: Equatable {
    case generatedSignal
    case localFile
}

@Observable
@MainActor
final class HostedAudioEngine {
    private(set) var isPlaying = false
    private(set) var sourceKind = HostedAudioSourceKind.generatedSignal
    private(set) var loadedFileName: String?
    private(set) var loadedFileDuration: TimeInterval = 0
    private(set) var playbackProgress: Double = 0
    private(set) var isLoadingFile = false
    private(set) var inputPeak: Float = 0
    private(set) var outputPeak: Float = 0
    private(set) var gainReductionDB: Float = 0
    private(set) var lastError: String?

    private let dsp: OpaquePointer
    private var audioEngine: AVAudioEngine?
    private var sourceNode: AVAudioSourceNode?
    private var localSource: LocalAudioSource?
    private var lastState: CompiledDSPState?
    private var lastEnabled = true
    private var resumeAfterInterruption = false
    private var meterTask: Task<Void, Never>?
    private var interruptionObserver: NSObjectProtocol?

    init() {
        guard let engine = PXEngineCreate() else {
            fatalError(HostedAudioEngineError.engineAllocationFailed.localizedDescription)
        }
        dsp = engine
        interruptionObserver = NotificationCenter.default.addObserver(
            forName: AVAudioSession.interruptionNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            Task { @MainActor in
                self?.handleInterruption(notification)
            }
        }
    }

    deinit {
        meterTask?.cancel()
        if let interruptionObserver {
            NotificationCenter.default.removeObserver(interruptionObserver)
        }
        PXEngineDestroy(dsp)
    }

    func loadLocalFile(from url: URL) async throws {
        isLoadingFile = true
        defer { isLoadingFile = false }

        let source = try await LocalAudioDecoder.decode(url)
        stop()
        localSource = source
        sourceKind = .localFile
        loadedFileName = source.displayName
        loadedFileDuration = source.duration
        playbackProgress = 0
        lastError = nil
    }

    func useGeneratedSignal() {
        stop()
        localSource = nil
        sourceKind = .generatedSignal
        loadedFileName = nil
        loadedFileDuration = 0
        playbackProgress = 0
        lastError = nil
    }

    func startSelectedSource(state: CompiledDSPState, enabled: Bool) {
        startSelectedSource(state: state, enabled: enabled, preservePosition: false)
    }

    func restartSelectedSource(state: CompiledDSPState, enabled: Bool) {
        let wasPlaying = isPlaying
        stop()
        if wasPlaying {
            startSelectedSource(state: state, enabled: enabled, preservePosition: true)
        }
    }

    private func startSelectedSource(
        state: CompiledDSPState,
        enabled: Bool,
        preservePosition: Bool
    ) {
        guard !isPlaying else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setPreferredSampleRate(48_000)
            try session.setPreferredIOBufferDuration(0.005)
            try session.setActive(true)

            let sampleRate = localSource?.sampleRate ?? session.sampleRate
            let channelCount = AVAudioChannelCount(localSource?.channelCount ?? 2)
            guard sampleRate > 0,
                  let format = AVAudioFormat(
                    commonFormat: .pcmFormatFloat32,
                    sampleRate: sampleRate,
                    channels: channelCount,
                    interleaved: false
                  ) else {
                throw HostedAudioEngineError.invalidOutputFormat
            }

            apply(state: state, enabled: enabled)
            let spec = PXProcessSpec(
                sampleRate: sampleRate,
                maximumFrameCount: 4_096,
                maximumChannelCount: Int(channelCount)
            )
            guard PXEnginePrepare(dsp, spec) else {
                throw HostedAudioEngineError.dspPreparationFailed
            }

            let engine = AVAudioEngine()
            let node: AVAudioSourceNode
            if let localSource {
                if !preservePosition && PXPCMSourceFinished(localSource.pointer) {
                    PXPCMSourceReset(localSource.pointer)
                    playbackProgress = 0
                }
                node = Self.makeLocalFileSourceNode(
                    dsp: dsp,
                    source: localSource.pointer,
                    format: format
                )
            } else {
                node = Self.makeGeneratedSourceNode(dsp: dsp, format: format)
            }
            engine.attach(node)
            engine.connect(node, to: engine.mainMixerNode, format: format)
            engine.prepare()
            try engine.start()

            audioEngine = engine
            sourceNode = node
            isPlaying = true
            lastError = nil
            startMeters()
        } catch {
            stop()
            lastError = error.localizedDescription
        }
    }

    func stop() {
        resumeAfterInterruption = false
        stopTransport()
    }

    private func stopTransport() {
        meterTask?.cancel()
        meterTask = nil
        audioEngine?.stop()
        if let engine = audioEngine, let sourceNode {
            engine.detach(sourceNode)
        }
        sourceNode = nil
        audioEngine = nil
        isPlaying = false
        inputPeak = 0
        outputPeak = 0
        gainReductionDB = 0
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    func apply(state: CompiledDSPState, enabled: Bool) {
        lastState = state
        lastEnabled = enabled
        PXEngineSetBypassed(dsp, !enabled)
        PXEngineSetInputGainDB(dsp, state.inputGainDB)
        PXEngineSetOutputGainDB(dsp, state.outputGainDB)
        PXEngineSetFilterEnabled(dsp, state.filterEnabled)
        PXEngineSetFilter(dsp, PXFilterTypeHighPass, state.highPassFrequency, 0.70710678, 0)
        PXEngineSetEqualizerEnabled(dsp, state.equalizerEnabled)
        _ = PXEngineSetEqualizerBandCount(dsp, 3)
        _ = PXEngineSetEqualizerBand(dsp, 0, true, PXFilterTypeLowShelf, 120, 0.70710678, state.lowGainDB)
        _ = PXEngineSetEqualizerBand(dsp, 1, true, PXFilterTypeBell, 2_800, 0.9, state.presenceGainDB)
        _ = PXEngineSetEqualizerBand(dsp, 2, true, PXFilterTypeHighShelf, 9_000, 0.70710678, state.airGainDB)
        PXEngineSetBassEnhancerEnabled(dsp, state.bassEnabled)
        PXEngineSetBassEnhancer(
            dsp,
            state.bassStrategy == .psychoacoustic
                ? PXBassEnhancerModePsychoacoustic : PXBassEnhancerModeExtension,
            state.bassCrossoverFrequency,
            state.bassAmount,
            state.bassDrive,
            state.bassMix
        )
        PXEngineSetExciterEnabled(dsp, state.exciterEnabled)
        PXEngineSetExciter(
            dsp,
            state.exciterFrequency,
            state.exciterDrive,
            state.exciterAmount,
            state.exciterMix
        )
        PXEngineSetCrystalizerEnabled(dsp, state.crystalizerEnabled)
        PXEngineSetCrystalizer(
            dsp,
            state.crystalizerFrequency,
            state.crystalizerAmount,
            state.crystalizerSensitivity,
            state.crystalizerMix
        )
        PXEngineSetStereoToolsEnabled(dsp, state.stereoEnabled)
        PXEngineSetStereoTools(
            dsp,
            state.stereoWidth,
            0, 0, 0,
            state.monoBassFrequency,
            state.monoBassAmount
        )
        PXEngineSetStereoSwitches(dsp, false, false, false, false)
        PXEngineSetLimiterEnabled(dsp, state.limiterEnabled)
        PXEngineSetLimiter(
            dsp,
            state.limiterCeilingDB,
            state.limiterLookaheadMilliseconds,
            state.limiterReleaseMilliseconds
        )
    }

    private func startMeters() {
        meterTask?.cancel()
        meterTask = Task { @MainActor [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                let meters = PXEngineMeters(dsp)
                inputPeak = meters.inputPeak
                outputPeak = meters.outputPeak
                gainReductionDB = meters.gainReductionDB
                if let localSource {
                    playbackProgress = min(
                        1,
                        Double(PXPCMSourcePosition(localSource.pointer))
                            / Double(max(localSource.frameCount, 1))
                    )
                    if PXPCMSourceFinished(localSource.pointer) {
                        playbackProgress = 1
                        stop()
                        return
                    }
                }
                try? await Task.sleep(for: .milliseconds(50))
            }
        }
    }

    private func handleInterruption(_ notification: Notification) {
        guard let value = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
              let type = AVAudioSession.InterruptionType(rawValue: value) else { return }
        if type == .began {
            let wasPlaying = isPlaying
            resumeAfterInterruption = wasPlaying
            if wasPlaying {
                stopTransport()
                lastError = "Playback paused for an audio interruption."
            }
        } else {
            let optionsValue = notification.userInfo?[AVAudioSessionInterruptionOptionKey]
                as? UInt ?? 0
            let options = AVAudioSession.InterruptionOptions(rawValue: optionsValue)
            let shouldResume = resumeAfterInterruption && options.contains(.shouldResume)
            resumeAfterInterruption = false
            guard shouldResume, let lastState else { return }
            startSelectedSource(
                state: lastState,
                enabled: lastEnabled,
                preservePosition: true
            )
        }
    }

    nonisolated private static func makeGeneratedSourceNode(
        dsp: OpaquePointer,
        format: AVAudioFormat
    ) -> AVAudioSourceNode {
        var phase = 0.0
        let increment = 2.0 * Double.pi * 220.0 / format.sampleRate
        return AVAudioSourceNode(format: format) { _, _, frameCount, outputData in
            let buffers = UnsafeMutableAudioBufferListPointer(outputData)
            for frame in 0..<Int(frameCount) {
                let fundamental = sin(phase) * 0.22
                let harmonic = sin(phase * 2.0) * 0.055
                let sample = Float(fundamental + harmonic)
                for buffer in buffers {
                    guard let data = buffer.mData else { return kAudio_ParamError }
                    data.assumingMemoryBound(to: Float.self)[frame] = sample
                }
                phase += increment
                if phase >= 2.0 * Double.pi {
                    phase -= 2.0 * Double.pi
                }
            }
            return PXEngineProcessAudioBufferList(dsp, outputData, frameCount) ? noErr : kAudio_ParamError
        }
    }

    nonisolated private static func makeLocalFileSourceNode(
        dsp: OpaquePointer,
        source: OpaquePointer,
        format: AVAudioFormat
    ) -> AVAudioSourceNode {
        AVAudioSourceNode(format: format) { _, _, frameCount, outputData in
            guard PXPCMSourceRenderAudioBufferList(source, outputData, frameCount, false) else {
                return kAudio_ParamError
            }
            return PXEngineProcessAudioBufferList(dsp, outputData, frameCount)
                ? noErr : kAudio_ParamError
        }
    }
}

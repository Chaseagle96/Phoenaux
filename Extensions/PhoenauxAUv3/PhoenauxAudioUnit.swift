import AudioToolbox
import AVFAudio
import Foundation

final class PhoenauxAudioUnit: AUAudioUnit {
    private let dsp: OpaquePointer
    private var inputBus: AUAudioUnitBus!
    private var outputBus: AUAudioUnitBus!
    private var inputBusArray: AUAudioUnitBusArray!
    private var outputBusArray: AUAudioUnitBusArray!
    private let phoenauxParameterTree = PhoenauxAUParameters.makeTree()
    private var selectedFactoryPreset: AUAudioUnitPreset?

    override var inputBusses: AUAudioUnitBusArray { inputBusArray }
    override var outputBusses: AUAudioUnitBusArray { outputBusArray }
    override var parameterTree: AUParameterTree? {
        get { phoenauxParameterTree }
        set { }
    }
    override var canProcessInPlace: Bool { true }
    override var tailTime: TimeInterval { 0 }
    override var supportsUserPresets: Bool { true }
    override var factoryPresets: [AUAudioUnitPreset]? { Self.presetDefinitions.map(\.preset) }
    override var currentPreset: AUAudioUnitPreset? {
        get { selectedFactoryPreset ?? super.currentPreset }
        set {
            guard let newValue,
                  let definition = Self.presetDefinitions.first(where: { $0.preset.number == newValue.number }) else {
                selectedFactoryPreset = nil
                super.currentPreset = newValue
                return
            }
            for (address, value) in definition.values {
                phoenauxParameterTree.parameter(withAddress: address.rawValue)?.value = value
            }
            selectedFactoryPreset = definition.preset
        }
    }
    override var latency: TimeInterval {
        let sampleRate = outputBus?.format.sampleRate ?? 0
        return sampleRate > 0 ? Double(PXEngineLatencyFrames(dsp)) / sampleRate : 0
    }

    override init(
        componentDescription: AudioComponentDescription,
        options: AudioComponentInstantiationOptions = []
    ) throws {
        guard let engine = PXEngineCreate() else {
            throw NSError(
                domain: "PhoenauxAUv3",
                code: -1,
                userInfo: [NSLocalizedDescriptionKey: "PhoenauxDSP allocation failed."]
            )
        }
        dsp = engine
        do {
            try super.init(componentDescription: componentDescription, options: options)
            guard let format = AVAudioFormat(
                commonFormat: .pcmFormatFloat32,
                sampleRate: 48_000,
                channels: 2,
                interleaved: false
            ) else {
                throw NSError(domain: "PhoenauxAUv3", code: -2)
            }
            inputBus = try AUAudioUnitBus(format: format)
            outputBus = try AUAudioUnitBus(format: format)
            inputBus.maximumChannelCount = 2
            outputBus.maximumChannelCount = 2
            inputBus.supportedChannelCounts = [1, 2]
            outputBus.supportedChannelCounts = [1, 2]
            inputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .input, busses: [inputBus])
            outputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
            _ = PXEngineSetEqualizerBandCount(dsp, 3)
            connectParameters()
        } catch {
            PXEngineDestroy(engine)
            throw error
        }
    }

    deinit {
        PXEngineDestroy(dsp)
    }

    override func shouldChange(to format: AVAudioFormat, for bus: AUAudioUnitBus) -> Bool {
        format.commonFormat == .pcmFormatFloat32
            && !format.isInterleaved
            && (1...2).contains(Int(format.channelCount))
            && format.sampleRate > 0
    }

    override func allocateRenderResources() throws {
        let inputFormat = inputBus.format
        let outputFormat = outputBus.format
        guard inputFormat.sampleRate == outputFormat.sampleRate,
              inputFormat.channelCount == outputFormat.channelCount,
              inputFormat.commonFormat == .pcmFormatFloat32,
              outputFormat.commonFormat == .pcmFormatFloat32,
              !inputFormat.isInterleaved,
              !outputFormat.isInterleaved else {
            throw NSError(
                domain: "PhoenauxAUv3",
                code: Int(kAudioUnitErr_FormatNotSupported),
                userInfo: [NSLocalizedDescriptionKey: "Phoenaux requires matching noninterleaved Float32 I/O."]
            )
        }
        try super.allocateRenderResources()
        let spec = PXProcessSpec(
            sampleRate: outputFormat.sampleRate,
            maximumFrameCount: Int(maximumFramesToRender),
            maximumChannelCount: Int(outputFormat.channelCount)
        )
        guard PXEnginePrepare(dsp, spec) else {
            super.deallocateRenderResources()
            throw NSError(
                domain: "PhoenauxAUv3",
                code: -3,
                userInfo: [NSLocalizedDescriptionKey: "PhoenauxDSP preparation failed."]
            )
        }
    }

    override func deallocateRenderResources() {
        PXEngineReset(dsp)
        super.deallocateRenderResources()
    }

    override func reset() {
        PXEngineReset(dsp)
        super.reset()
    }

    override var internalRenderBlock: AUInternalRenderBlock {
        let dsp = dsp
        let maximumFrames = maximumFramesToRender
        return { actionFlags, timestamp, frameCount, outputBusNumber, outputData, _, pullInputBlock in
            guard outputBusNumber == 0 else { return kAudioUnitErr_InvalidElement }
            guard frameCount <= maximumFrames else { return kAudioUnitErr_TooManyFramesToProcess }
            guard let pullInputBlock else { return kAudioUnitErr_NoConnection }
            let status = pullInputBlock(actionFlags, timestamp, frameCount, 0, outputData)
            guard status == noErr else { return status }
            return PXEngineProcessAudioBufferList(dsp, outputData, frameCount)
                ? noErr : kAudio_ParamError
        }
    }

    private func connectParameters() {
        phoenauxParameterTree.implementorValueObserver = { [weak self] parameter, value in
            self?.apply(address: parameter.address, value: value)
        }
        for parameter in phoenauxParameterTree.allParameters {
            apply(address: parameter.address, value: parameter.value)
        }
    }

    private func apply(address: AUParameterAddress, value: AUValue) {
        guard let address = PhoenauxAUParameterAddress(rawValue: address) else { return }
        switch address {
        case .bypass: PXEngineSetBypassed(dsp, value >= 0.5)
        case .inputGainDB: PXEngineSetInputGainDB(dsp, value)
        case .outputGainDB: PXEngineSetOutputGainDB(dsp, value)
        case .filterEnabled: PXEngineSetFilterEnabled(dsp, value >= 0.5)
        case .highPassFrequency: PXEngineSetFilter(dsp, PXFilterTypeHighPass, value, 0.70710678, 0)
        case .equalizerEnabled: PXEngineSetEqualizerEnabled(dsp, value >= 0.5)
        case .lowGainDB: _ = PXEngineSetEqualizerBand(dsp, 0, true, PXFilterTypeLowShelf, 120, 0.70710678, value)
        case .presenceGainDB: _ = PXEngineSetEqualizerBand(dsp, 1, true, PXFilterTypeBell, 2_800, 0.9, value)
        case .airGainDB: _ = PXEngineSetEqualizerBand(dsp, 2, true, PXFilterTypeHighShelf, 9_000, 0.70710678, value)
        case .bassEnabled: PXEngineSetBassEnhancerEnabled(dsp, value >= 0.5)
        case .bassAmount: PXEngineSetBassEnhancer(dsp, PXBassEnhancerModeExtension, 95, value, 2.6, 0.55)
        case .exciterEnabled: PXEngineSetExciterEnabled(dsp, value >= 0.5)
        case .exciterAmount: PXEngineSetExciter(dsp, 4_500, 2.4, value, 0.35)
        case .crystalizerEnabled: PXEngineSetCrystalizerEnabled(dsp, value >= 0.5)
        case .crystalizerAmount: PXEngineSetCrystalizer(dsp, 2_500, value, 0.62, 0.45)
        case .stereoEnabled: PXEngineSetStereoToolsEnabled(dsp, value >= 0.5)
        case .stereoWidth: PXEngineSetStereoTools(dsp, value, 0, 0, 0, 120, 1)
        case .limiterEnabled: PXEngineSetLimiterEnabled(dsp, value >= 0.5)
        case .limiterCeilingDB: PXEngineSetLimiter(dsp, value, 5, 100)
        }
    }

    private struct PresetDefinition {
        let preset: AUAudioUnitPreset
        let values: [PhoenauxAUParameterAddress: AUValue]
    }

    private static let presetDefinitions: [PresetDefinition] = [
        preset(0, "Reborn", -4, 2, 1.5, 1, 0.36, 0.18, 0.24, 1.12),
        preset(1, "Pure", -2, 0.5, 0.5, 0.5, 0.14, 0.08, 0.09, 1.04),
        preset(2, "Impact", -5, 3.5, 1, 0, 0.54, 0.12, 0.29, 1.08),
        preset(3, "Crystal", -4, 0.5, 2, 2.5, 0.17, 0.36, 0.54, 1.10),
        preset(4, "Wide", -3, 1, 0.5, 1, 0.23, 0.16, 0.21, 1.22),
        preset(5, "Voice", -3, -1, 3, 0.5, 0.08, 0.20, 0.20, 1.02),
        preset(6, "Night", -5, 2.5, 1.5, -0.5, 0.32, 0.10, 0.15, 1.06),
    ]

    private static func preset(
        _ number: Int,
        _ name: String,
        _ inputGain: AUValue,
        _ low: AUValue,
        _ presence: AUValue,
        _ air: AUValue,
        _ bass: AUValue,
        _ exciter: AUValue,
        _ crystalizer: AUValue,
        _ width: AUValue
    ) -> PresetDefinition {
        .init(
            preset: AUAudioUnitPreset(number: number, name: name),
            values: [
                .inputGainDB: inputGain,
                .lowGainDB: low,
                .presenceGainDB: presence,
                .airGainDB: air,
                .bassAmount: bass,
                .exciterAmount: exciter,
                .crystalizerAmount: crystalizer,
                .stereoWidth: width,
            ]
        )
    }
}

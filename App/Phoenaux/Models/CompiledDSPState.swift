import Foundation

struct CompiledDSPState: Equatable, Sendable {
    let presetIdentifier: String
    let profileIdentifier: String
    let profileRevision: Int
    let intensity: Float
    let inputGainDB: Float
    let outputGainDB: Float
    let filterEnabled: Bool
    let highPassFrequency: Float
    let equalizerEnabled: Bool
    let lowGainDB: Float
    let presenceGainDB: Float
    let airGainDB: Float
    let bassEnabled: Bool
    let bassStrategy: ProfileBassStrategy
    let bassCrossoverFrequency: Float
    let bassAmount: Float
    let bassDrive: Float
    let bassMix: Float
    let exciterEnabled: Bool
    let exciterFrequency: Float
    let exciterDrive: Float
    let exciterAmount: Float
    let exciterMix: Float
    let crystalizerEnabled: Bool
    let crystalizerFrequency: Float
    let crystalizerAmount: Float
    let crystalizerSensitivity: Float
    let crystalizerMix: Float
    let stereoEnabled: Bool
    let stereoWidth: Float
    let monoBassFrequency: Float
    let monoBassAmount: Float
    let limiterEnabled: Bool
    let limiterCeilingDB: Float
    let limiterLookaheadMilliseconds: Float
    let limiterReleaseMilliseconds: Float
}
enum DSPStateCompiler {
    static let supportedGraphOrder = DSPModuleKind.allCases

    static func compile(
        preset source: PhoenauxPresetDocument,
        profile: ResolvedDeviceProfile,
        intensity rawIntensity: Double
    ) throws -> CompiledDSPState {
        let preset = try source.validated()
        guard preset.graphOrder == supportedGraphOrder else {
            throw PresetDocumentError.unsupportedGraphOrder
        }
        let intensity = Float(max(0, min(rawIntensity, 1)))
        let tonalCurve = smoothstep(intensity)
        let bassCurve = pow(intensity, 0.82)
        let detailCurve = pow(intensity, 1.25)
        let widthCurve = smoothstep(smoothstep(intensity))

        let filter = try requiredModule(.filter, in: preset)
        let equalizer = try requiredModule(.equalizer, in: preset)
        let bass = try requiredModule(.bassEnhancer, in: preset)
        let exciter = try requiredModule(.exciter, in: preset)
        let crystalizer = try requiredModule(.crystalizer, in: preset)
        let stereo = try requiredModule(.stereoTools, in: preset)
        let limiter = try requiredModule(.limiter, in: preset)

        let authoredWidth = value("width", in: stereo, default: 1)
        let width = min(profile.maximumStereoWidth, 1 + (authoredWidth - 1) * widthCurve)
        return .init(
            presetIdentifier: preset.identifier,
            profileIdentifier: profile.identifier,
            profileRevision: profile.revision,
            intensity: intensity,
            inputGainDB: clamp(preset.inputGainDB * tonalCurve + profile.inputHeadroomDB, -24, 0),
            outputGainDB: clamp(preset.outputGainDB, -24, 0),
            filterEnabled: filter.enabled,
            highPassFrequency: max(profile.highPassFrequency, value("frequency", in: filter, default: 25)),
            equalizerEnabled: equalizer.enabled,
            lowGainDB: clamp(value("lowGainDB", in: equalizer) * bassCurve, -12, 12),
            presenceGainDB: clamp(
                value("presenceGainDB", in: equalizer) * tonalCurve + profile.presenceGainOffsetDB * tonalCurve,
                -12, 12
            ),
            airGainDB: clamp(
                value("airGainDB", in: equalizer) * detailCurve + profile.airGainOffsetDB * detailCurve,
                -12, 12
            ),
            bassEnabled: bass.enabled,
            bassStrategy: profile.bassStrategy,
            bassCrossoverFrequency: profile.bassCrossoverFrequency,
            bassAmount: clamp(value("amount", in: bass) * profile.bassAmountScale * bassCurve, 0, 1),
            bassDrive: clamp(value("drive", in: bass, default: 2), 1, 6),
            bassMix: clamp(value("mix", in: bass, default: 0.5), 0, 1),
            exciterEnabled: exciter.enabled,
            exciterFrequency: clamp(value("frequency", in: exciter, default: 4_500), 1_000, 18_000),
            exciterDrive: clamp(value("drive", in: exciter, default: 2), 1, 8),
            exciterAmount: clamp(value("amount", in: exciter) * detailCurve, 0, 1),
            exciterMix: clamp(value("mix", in: exciter, default: 0.35), 0, 1),
            crystalizerEnabled: crystalizer.enabled,
            crystalizerFrequency: clamp(value("frequency", in: crystalizer, default: 2_500), 500, 16_000),
            crystalizerAmount: clamp(
                value("amount", in: crystalizer) * profile.crystalizerScale * detailCurve,
                0, 1.5
            ),
            crystalizerSensitivity: clamp(value("sensitivity", in: crystalizer, default: 0.5), 0, 1),
            crystalizerMix: clamp(value("mix", in: crystalizer, default: 0.35), 0, 1),
            stereoEnabled: stereo.enabled,
            stereoWidth: width,
            monoBassFrequency: clamp(value("monoBassFrequency", in: stereo, default: 120), 40, 400),
            monoBassAmount: clamp(value("monoBassAmount", in: stereo, default: 1), 0, 1),
            limiterEnabled: limiter.enabled,
            limiterCeilingDB: clamp(value("ceilingDB", in: limiter, default: -1), -24, 0),
            limiterLookaheadMilliseconds: clamp(
                value("lookaheadMilliseconds", in: limiter, default: 5), 0.1, 20
            ),
            limiterReleaseMilliseconds: clamp(
                value("releaseMilliseconds", in: limiter, default: 100), 10, 1_000
            )
        )
    }

    private static func requiredModule(
        _ kind: DSPModuleKind,
        in preset: PhoenauxPresetDocument
    ) throws -> PresetModuleState {
        guard let module = preset.module(kind) else {
            throw PresetDocumentError.invalidModules
        }
        return module
    }

    private static func value(
        _ key: String,
        in module: PresetModuleState,
        default defaultValue: Float = 0
    ) -> Float {
        module.parameters[key] ?? defaultValue
    }

    private static func smoothstep(_ value: Float) -> Float {
        value * value * (3 - 2 * value)
    }

    private static func clamp(_ value: Float, _ lower: Float, _ upper: Float) -> Float {
        min(upper, max(lower, value))
    }
}

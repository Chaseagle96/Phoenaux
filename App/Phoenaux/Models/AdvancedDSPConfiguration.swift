import Foundation

enum AdvancedDSPParameter: String, CaseIterable, Identifiable, Sendable {
    case inputGainDB
    case outputGainDB
    case highPassFrequency
    case lowGainDB
    case presenceGainDB
    case airGainDB
    case bassAmount
    case exciterAmount
    case crystalizerAmount
    case stereoWidth
    case limiterCeilingDB

    var id: Self { self }

    var displayName: String {
        switch self {
        case .inputGainDB: "Input trim"
        case .outputGainDB: "Output trim"
        case .highPassFrequency: "High-pass frequency"
        case .lowGainDB: "Low EQ"
        case .presenceGainDB: "Presence EQ"
        case .airGainDB: "Air EQ"
        case .bassAmount: "Bass amount"
        case .exciterAmount: "Exciter amount"
        case .crystalizerAmount: "Crystalizer amount"
        case .stereoWidth: "Stereo width"
        case .limiterCeilingDB: "Limiter ceiling"
        }
    }

    var range: ClosedRange<Float> {
        switch self {
        case .inputGainDB, .outputGainDB: -24...0
        case .highPassFrequency: 20...200
        case .lowGainDB, .presenceGainDB, .airGainDB: -12...12
        case .bassAmount, .exciterAmount: 0...1
        case .crystalizerAmount: 0...1.5
        case .stereoWidth: 0.5...1.5
        case .limiterCeilingDB: -6...0
        }
    }

    var step: Float {
        switch self {
        case .highPassFrequency: 1
        case .inputGainDB, .outputGainDB, .lowGainDB, .presenceGainDB, .airGainDB,
             .limiterCeilingDB: 0.1
        case .bassAmount, .exciterAmount, .crystalizerAmount, .stereoWidth: 0.01
        }
    }

    var defaultValue: Float {
        switch self {
        case .inputGainDB, .outputGainDB, .lowGainDB, .presenceGainDB, .airGainDB: 0
        case .highPassFrequency: 25
        case .bassAmount, .exciterAmount, .crystalizerAmount: 0
        case .stereoWidth: 1
        case .limiterCeilingDB: -1
        }
    }

    var moduleKind: DSPModuleKind? {
        switch self {
        case .inputGainDB, .outputGainDB: nil
        case .highPassFrequency: .filter
        case .lowGainDB, .presenceGainDB, .airGainDB: .equalizer
        case .bassAmount: .bassEnhancer
        case .exciterAmount: .exciter
        case .crystalizerAmount: .crystalizer
        case .stereoWidth: .stereoTools
        case .limiterCeilingDB: .limiter
        }
    }

    var parameterKey: String? {
        switch self {
        case .inputGainDB, .outputGainDB: nil
        case .highPassFrequency: "frequency"
        case .lowGainDB: "lowGainDB"
        case .presenceGainDB: "presenceGainDB"
        case .airGainDB: "airGainDB"
        case .bassAmount, .exciterAmount, .crystalizerAmount: "amount"
        case .stereoWidth: "width"
        case .limiterCeilingDB: "ceilingDB"
        }
    }

    func clamped(_ value: Float) -> Float {
        min(range.upperBound, max(range.lowerBound, value.isFinite ? value : defaultValue))
    }
}

extension PhoenauxPresetDocument {
    func authoredValue(for parameter: AdvancedDSPParameter) -> Float {
        switch parameter {
        case .inputGainDB: return inputGainDB
        case .outputGainDB: return outputGainDB
        default:
            guard let kind = parameter.moduleKind,
                  let key = parameter.parameterKey else {
                return parameter.defaultValue
            }
            return module(kind)?.parameters[key] ?? parameter.defaultValue
        }
    }

    func applyingAdvancedOverrides(
        moduleEnabled: [DSPModuleKind: Bool],
        parameters: [AdvancedDSPParameter: Float]
    ) -> Self {
        var result = self
        for (kind, enabled) in moduleEnabled {
            guard let index = result.modules.firstIndex(where: { $0.kind == kind }) else { continue }
            result.modules[index].enabled = enabled
        }
        for (parameter, rawValue) in parameters {
            let value = parameter.clamped(rawValue)
            switch parameter {
            case .inputGainDB:
                result.inputGainDB = value
            case .outputGainDB:
                result.outputGainDB = value
            default:
                guard let kind = parameter.moduleKind,
                      let key = parameter.parameterKey,
                      let index = result.modules.firstIndex(where: { $0.kind == kind }) else {
                    continue
                }
                result.modules[index].parameters[key] = value
            }
        }
        return result
    }
}

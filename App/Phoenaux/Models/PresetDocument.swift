import Foundation

enum DSPModuleKind: String, CaseIterable, Codable, Hashable, Sendable {
    case filter
    case equalizer
    case bassEnhancer
    case exciter
    case crystalizer
    case stereoTools
    case limiter
}

struct PresetModuleState: Codable, Equatable, Sendable {
    let kind: DSPModuleKind
    var enabled: Bool
    var parameters: [String: Float]
}

struct PhoenauxPresetDocument: Codable, Identifiable, Equatable, Sendable {
    static let currentSchemaVersion = 1

    let schemaVersion: Int
    let identifier: String
    var revision: Int
    var name: String
    var profileCompatibility: [String]
    var graphOrder: [DSPModuleKind]
    var modules: [PresetModuleState]
    var inputGainDB: Float
    var outputGainDB: Float
    var authoredIntensity: Float
    var notes: String? = nil

    var id: String { identifier }

    func validated() throws -> Self {
        guard schemaVersion == Self.currentSchemaVersion else {
            throw PresetDocumentError.unsupportedSchema(schemaVersion)
        }
        guard identifier.range(of: #"^[A-Za-z0-9][A-Za-z0-9._-]{2,127}$"#,
                               options: .regularExpression) != nil else {
            throw PresetDocumentError.invalidIdentifier
        }
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty, trimmedName.count <= 80 else {
            throw PresetDocumentError.invalidName
        }
        guard revision > 0, authoredIntensity.isFinite, (0...1).contains(authoredIntensity),
              inputGainDB.isFinite, (-24...6).contains(inputGainDB),
              outputGainDB.isFinite, (-24...6).contains(outputGainDB) else {
            throw PresetDocumentError.invalidGainOrIntensity
        }
        guard graphOrder.count == DSPModuleKind.allCases.count,
              Set(graphOrder) == Set(DSPModuleKind.allCases) else {
            throw PresetDocumentError.invalidGraph
        }
        guard modules.count == DSPModuleKind.allCases.count,
              Set(modules.map(\.kind)) == Set(DSPModuleKind.allCases) else {
            throw PresetDocumentError.invalidModules
        }
        for module in modules {
            guard module.parameters.count <= 64,
                  module.parameters.keys.allSatisfy({ !$0.isEmpty && $0.count <= 64 }),
                  module.parameters.values.allSatisfy({ $0.isFinite && abs($0) <= 100_000 }) else {
                throw PresetDocumentError.invalidParameters(module.kind)
            }
        }
        var result = self
        result.name = trimmedName
        result.profileCompatibility = Array(Set(profileCompatibility)).sorted()
        return result
    }

    func module(_ kind: DSPModuleKind) -> PresetModuleState? {
        modules.first { $0.kind == kind }
    }
}

enum PresetDocumentError: LocalizedError {
    case unsupportedSchema(Int)
    case invalidIdentifier
    case invalidName
    case invalidGainOrIntensity
    case invalidGraph
    case invalidModules
    case invalidParameters(DSPModuleKind)
    case unsupportedGraphOrder

    var errorDescription: String? {
        switch self {
        case let .unsupportedSchema(version): "Unsupported preset schema version: \(version)"
        case .invalidIdentifier: "Preset identifier is invalid."
        case .invalidName: "Preset name is empty or too long."
        case .invalidGainOrIntensity: "Preset gain or intensity is outside the safe range."
        case .invalidGraph: "Preset graph must contain every flagship module exactly once."
        case .invalidModules: "Preset module states are incomplete or duplicated."
        case let .invalidParameters(kind): "Preset contains invalid parameters for \(kind.rawValue)."
        case .unsupportedGraphOrder: "This build cannot yet render the preset's custom graph order."
        }
    }
}

extension PhoenauxPreset {
    var document: PhoenauxPresetDocument {
        let eq = eqGainsDB
        let creative = creativeTuning
        return .init(
            schemaVersion: PhoenauxPresetDocument.currentSchemaVersion,
            identifier: "com.phoenaux.preset.\(rawValue.lowercased())",
            revision: 1,
            name: rawValue,
            profileCompatibility: [DeviceProfileCatalog.genericIdentifier],
            graphOrder: DSPModuleKind.allCases,
            modules: [
                .init(kind: .filter, enabled: true, parameters: [
                    "frequency": 28, "q": 0.70710678,
                ]),
                .init(kind: .equalizer, enabled: true, parameters: [
                    "lowGainDB": eq.low,
                    "presenceGainDB": eq.presence,
                    "airGainDB": eq.air,
                ]),
                .init(kind: .bassEnhancer, enabled: true, parameters: [
                    "amount": creative.bassAmount,
                    "drive": creative.bassDrive,
                    "mix": 0.55,
                ]),
                .init(kind: .exciter, enabled: true, parameters: [
                    "frequency": 4_500,
                    "drive": 2.4,
                    "amount": creative.exciterAmount,
                    "mix": 0.35,
                ]),
                .init(kind: .crystalizer, enabled: true, parameters: [
                    "frequency": 2_500,
                    "amount": creative.crystalizerAmount,
                    "sensitivity": 0.62,
                    "mix": 0.45,
                ]),
                .init(kind: .stereoTools, enabled: true, parameters: [
                    "width": creative.stereoWidth,
                    "monoBassFrequency": 120,
                    "monoBassAmount": 1,
                ]),
                .init(kind: .limiter, enabled: true, parameters: [
                    "ceilingDB": -1,
                    "lookaheadMilliseconds": 5,
                    "releaseMilliseconds": 100,
                ]),
            ],
            inputGainDB: inputGainDB,
            outputGainDB: 0,
            authoredIntensity: 1,
            notes: purpose
        )
    }
}

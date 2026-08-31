import Foundation

enum PhoenauxPreset: String, CaseIterable, Identifiable, Codable, Sendable {
    case reborn = "Reborn"
    case pure = "Pure"
    case impact = "Impact"
    case crystal = "Crystal"
    case wide = "Wide"
    case voice = "Voice"
    case night = "Night"

    var id: Self { self }

    var purpose: String {
        switch self {
        case .reborn: "Balanced, vivid, and immediately engaging"
        case .pure: "Subtle refinement with generous headroom"
        case .impact: "Controlled bass weight and punch"
        case .crystal: "Presence, air, and transient articulation"
        case .wide: "Immersion with responsible mono compatibility"
        case .voice: "Dialogue and vocal intelligibility"
        case .night: "Fuller perception at lower listening levels"
        }
    }

    var inputGainDB: Float {
        switch self {
        case .reborn: -4.0
        case .pure: -2.0
        case .impact: -5.0
        case .crystal: -4.0
        case .wide: -3.0
        case .voice: -3.0
        case .night: -5.0
        }
    }

    var eqGainsDB: (low: Float, presence: Float, air: Float) {
        switch self {
        case .reborn: (2.0, 1.5, 1.0)
        case .pure: (0.5, 0.5, 0.5)
        case .impact: (3.5, 1.0, 0.0)
        case .crystal: (0.5, 2.0, 2.5)
        case .wide: (1.0, 0.5, 1.0)
        case .voice: (-1.0, 3.0, 0.5)
        case .night: (2.5, 1.5, -0.5)
        }
    }

    var creativeTuning: PresetCreativeTuning {
        switch self {
        case .reborn:
            .init(bassAmount: 0.48, bassDrive: 2.6, exciterAmount: 0.18,
                  crystalizerAmount: 0.32, stereoWidth: 1.12)
        case .pure:
            .init(bassAmount: 0.18, bassDrive: 1.8, exciterAmount: 0.08,
                  crystalizerAmount: 0.12, stereoWidth: 1.04)
        case .impact:
            .init(bassAmount: 0.72, bassDrive: 3.2, exciterAmount: 0.12,
                  crystalizerAmount: 0.38, stereoWidth: 1.08)
        case .crystal:
            .init(bassAmount: 0.22, bassDrive: 2.0, exciterAmount: 0.36,
                  crystalizerAmount: 0.72, stereoWidth: 1.10)
        case .wide:
            .init(bassAmount: 0.30, bassDrive: 2.2, exciterAmount: 0.16,
                  crystalizerAmount: 0.28, stereoWidth: 1.38)
        case .voice:
            .init(bassAmount: 0.10, bassDrive: 1.6, exciterAmount: 0.20,
                  crystalizerAmount: 0.26, stereoWidth: 1.02)
        case .night:
            .init(bassAmount: 0.42, bassDrive: 2.0, exciterAmount: 0.10,
                  crystalizerAmount: 0.20, stereoWidth: 1.06)
        }
    }
}

struct PresetCreativeTuning: Sendable {
    let bassAmount: Float
    let bassDrive: Float
    let exciterAmount: Float
    let crystalizerAmount: Float
    let stereoWidth: Float
}

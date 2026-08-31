import Foundation

enum ProfileBassStrategy: String, Codable, Sendable {
    case psychoacoustic
    case extensionBass = "extension"
}

enum ProfileRouteFamily: String, Codable, Sendable {
    case generic
    case iPhoneSpeaker
    case airPods
    case airPodsPro
    case airPodsPro3
    case bluetoothHeadphones
    case bluetoothSpeaker
    case wiredHeadphones
    case usbAudio
    case airPlay
}

struct DeviceProfileOverrides: Codable, Equatable, Sendable {
    var highPassFrequency: Float? = nil
    var bassStrategy: ProfileBassStrategy? = nil
    var bassCrossoverFrequency: Float? = nil
    var bassAmountScale: Float? = nil
    var presenceGainOffsetDB: Float? = nil
    var airGainOffsetDB: Float? = nil
    var crystalizerScale: Float? = nil
    var maximumStereoWidth: Float? = nil
    var inputHeadroomDB: Float? = nil
}

struct DeviceProfileDefinition: Codable, Identifiable, Equatable, Sendable {
    let schemaVersion: Int
    let identifier: String
    let revision: Int
    let displayName: String
    let parentIdentifier: String?
    let routeFamily: ProfileRouteFamily
    let modelIdentifier: String?
    let supportedSampleRates: [Double]
    let supportedChannelCounts: [Int]
    let tuningProvenance: String
    let overrides: DeviceProfileOverrides

    var id: String { identifier }
}

struct ResolvedDeviceProfile: Identifiable, Equatable, Sendable {
    let identifier: String
    let revision: Int
    let displayName: String
    let ancestry: [String]
    let routeFamily: ProfileRouteFamily
    let supportedSampleRates: [Double]
    let supportedChannelCounts: [Int]
    let tuningProvenance: String
    let highPassFrequency: Float
    let bassStrategy: ProfileBassStrategy
    let bassCrossoverFrequency: Float
    let bassAmountScale: Float
    let presenceGainOffsetDB: Float
    let airGainOffsetDB: Float
    let crystalizerScale: Float
    let maximumStereoWidth: Float
    let inputHeadroomDB: Float

    var id: String { identifier }
}

enum DeviceProfileError: LocalizedError {
    case unknownProfile(String)
    case inheritanceCycle(String)
    case incompleteProfile(String)

    var errorDescription: String? {
        switch self {
        case let .unknownProfile(identifier): "Unknown device profile: \(identifier)"
        case let .inheritanceCycle(identifier): "Device profile inheritance cycle at: \(identifier)"
        case let .incompleteProfile(identifier): "Device profile lacks required inherited values: \(identifier)"
        }
    }
}

enum DeviceProfileCatalog {
    static let genericIdentifier = "com.phoenaux.profile.generic"
    static let iPhoneSpeakerIdentifier = "com.phoenaux.profile.iphone-speaker.generic"
    static let airPodsPro3Identifier = "com.phoenaux.profile.airpods-pro-3.provisional"

    static let definitions: [DeviceProfileDefinition] = [
        .init(
            schemaVersion: 1,
            identifier: genericIdentifier,
            revision: 1,
            displayName: "Conservative Generic",
            parentIdentifier: nil,
            routeFamily: .generic,
            modelIdentifier: nil,
            supportedSampleRates: [44_100, 48_000, 96_000],
            supportedChannelCounts: [1, 2],
            tuningProvenance: "Conservative engineering defaults; no device correction claim.",
            overrides: .init(
                highPassFrequency: 25,
                bassStrategy: .extensionBass,
                bassCrossoverFrequency: 100,
                bassAmountScale: 0.75,
                presenceGainOffsetDB: 0,
                airGainOffsetDB: 0,
                crystalizerScale: 0.75,
                maximumStereoWidth: 1.16,
                inputHeadroomDB: -1
            )
        ),
        .init(
            schemaVersion: 1,
            identifier: iPhoneSpeakerIdentifier,
            revision: 1,
            displayName: "Generic iPhone Speaker",
            parentIdentifier: genericIdentifier,
            routeFamily: .iPhoneSpeaker,
            modelIdentifier: nil,
            supportedSampleRates: [44_100, 48_000],
            supportedChannelCounts: [1, 2],
            tuningProvenance: "Provisional protective profile pending model-specific acoustic measurements.",
            overrides: .init(
                highPassFrequency: 55,
                bassStrategy: .psychoacoustic,
                bassCrossoverFrequency: 115,
                bassAmountScale: 0.82,
                presenceGainOffsetDB: 0.5,
                airGainOffsetDB: -0.25,
                crystalizerScale: 0.8,
                maximumStereoWidth: 1.10,
                inputHeadroomDB: -1.5
            )
        ),
        .init(
            schemaVersion: 1,
            identifier: "com.phoenaux.profile.airpods",
            revision: 1,
            displayName: "AirPods Family",
            parentIdentifier: genericIdentifier,
            routeFamily: .airPods,
            modelIdentifier: nil,
            supportedSampleRates: [44_100, 48_000],
            supportedChannelCounts: [2],
            tuningProvenance: "Conservative family fallback; no exact-model correction.",
            overrides: .init(
                highPassFrequency: 20,
                bassStrategy: .extensionBass,
                bassCrossoverFrequency: 95,
                bassAmountScale: 0.9,
                presenceGainOffsetDB: 0,
                airGainOffsetDB: 0,
                crystalizerScale: 0.9,
                maximumStereoWidth: 1.22,
                inputHeadroomDB: -0.5
            )
        ),
        .init(
            schemaVersion: 1,
            identifier: "com.phoenaux.profile.airpods-pro",
            revision: 1,
            displayName: "AirPods Pro Family",
            parentIdentifier: "com.phoenaux.profile.airpods",
            routeFamily: .airPodsPro,
            modelIdentifier: nil,
            supportedSampleRates: [48_000],
            supportedChannelCounts: [2],
            tuningProvenance: "Pro-family fallback accounting for already-extended bass and Apple processing.",
            overrides: .init(
                highPassFrequency: nil,
                bassStrategy: nil,
                bassCrossoverFrequency: 90,
                bassAmountScale: 0.82,
                presenceGainOffsetDB: -0.25,
                airGainOffsetDB: 0,
                crystalizerScale: 0.82,
                maximumStereoWidth: 1.18,
                inputHeadroomDB: nil
            )
        ),
        .init(
            schemaVersion: 1,
            identifier: airPodsPro3Identifier,
            revision: 1,
            displayName: "AirPods Pro 3 — Provisional",
            parentIdentifier: "com.phoenaux.profile.airpods-pro",
            routeFamily: .airPodsPro3,
            modelIdentifier: "best-effort-route-name",
            supportedSampleRates: [48_000],
            supportedChannelCounts: [2],
            tuningProvenance: "Provisional inheritance only; requires physical-device measurement and ANC-mode validation.",
            overrides: .init(
                highPassFrequency: nil,
                bassStrategy: .extensionBass,
                bassCrossoverFrequency: 85,
                bassAmountScale: 0.78,
                presenceGainOffsetDB: -0.35,
                airGainOffsetDB: -0.15,
                crystalizerScale: 0.78,
                maximumStereoWidth: 1.16,
                inputHeadroomDB: -0.75
            )
        ),
    ]

    static func resolve(for route: PhoenauxOutputRoute) -> ResolvedDeviceProfile {
        let identifier: String
        switch route {
        case .iPhoneSpeaker:
            identifier = iPhoneSpeakerIdentifier
        case .airPodsPro3:
            identifier = airPodsPro3Identifier
        case .airPodsProFamily:
            identifier = "com.phoenaux.profile.airpods-pro"
        default:
            identifier = genericIdentifier
        }
        return (try? resolve(identifier: identifier)) ?? fallback
    }

    static func resolve(identifier: String) throws -> ResolvedDeviceProfile {
        let byIdentifier = Dictionary(uniqueKeysWithValues: definitions.map { ($0.identifier, $0) })
        var ancestry: [DeviceProfileDefinition] = []
        var visited: Set<String> = []
        var currentIdentifier: String? = identifier
        while let current = currentIdentifier {
            guard visited.insert(current).inserted else {
                throw DeviceProfileError.inheritanceCycle(current)
            }
            guard let definition = byIdentifier[current] else {
                throw DeviceProfileError.unknownProfile(current)
            }
            ancestry.append(definition)
            currentIdentifier = definition.parentIdentifier
        }
        ancestry.reverse()

        var merged = DeviceProfileOverrides()
        for definition in ancestry {
            merged.highPassFrequency = definition.overrides.highPassFrequency ?? merged.highPassFrequency
            merged.bassStrategy = definition.overrides.bassStrategy ?? merged.bassStrategy
            merged.bassCrossoverFrequency = definition.overrides.bassCrossoverFrequency ?? merged.bassCrossoverFrequency
            merged.bassAmountScale = definition.overrides.bassAmountScale ?? merged.bassAmountScale
            merged.presenceGainOffsetDB = definition.overrides.presenceGainOffsetDB ?? merged.presenceGainOffsetDB
            merged.airGainOffsetDB = definition.overrides.airGainOffsetDB ?? merged.airGainOffsetDB
            merged.crystalizerScale = definition.overrides.crystalizerScale ?? merged.crystalizerScale
            merged.maximumStereoWidth = definition.overrides.maximumStereoWidth ?? merged.maximumStereoWidth
            merged.inputHeadroomDB = definition.overrides.inputHeadroomDB ?? merged.inputHeadroomDB
        }
        guard let highPassFrequency = merged.highPassFrequency,
              let bassStrategy = merged.bassStrategy,
              let bassCrossoverFrequency = merged.bassCrossoverFrequency,
              let bassAmountScale = merged.bassAmountScale,
              let presenceGainOffsetDB = merged.presenceGainOffsetDB,
              let airGainOffsetDB = merged.airGainOffsetDB,
              let crystalizerScale = merged.crystalizerScale,
              let maximumStereoWidth = merged.maximumStereoWidth,
              let inputHeadroomDB = merged.inputHeadroomDB,
              let leaf = ancestry.last else {
            throw DeviceProfileError.incompleteProfile(identifier)
        }
        return .init(
            identifier: leaf.identifier,
            revision: leaf.revision,
            displayName: leaf.displayName,
            ancestry: ancestry.map(\.identifier),
            routeFamily: leaf.routeFamily,
            supportedSampleRates: leaf.supportedSampleRates,
            supportedChannelCounts: leaf.supportedChannelCounts,
            tuningProvenance: leaf.tuningProvenance,
            highPassFrequency: highPassFrequency,
            bassStrategy: bassStrategy,
            bassCrossoverFrequency: bassCrossoverFrequency,
            bassAmountScale: bassAmountScale,
            presenceGainOffsetDB: presenceGainOffsetDB,
            airGainOffsetDB: airGainOffsetDB,
            crystalizerScale: crystalizerScale,
            maximumStereoWidth: maximumStereoWidth,
            inputHeadroomDB: inputHeadroomDB
        )
    }

    private static let fallback = ResolvedDeviceProfile(
        identifier: genericIdentifier,
        revision: 1,
        displayName: "Conservative Generic",
        ancestry: [genericIdentifier],
        routeFamily: .generic,
        supportedSampleRates: [44_100, 48_000],
        supportedChannelCounts: [1, 2],
        tuningProvenance: "Hardcoded safe fallback.",
        highPassFrequency: 25,
        bassStrategy: .extensionBass,
        bassCrossoverFrequency: 100,
        bassAmountScale: 0.75,
        presenceGainOffsetDB: 0,
        airGainOffsetDB: 0,
        crystalizerScale: 0.75,
        maximumStereoWidth: 1.16,
        inputHeadroomDB: -1
    )
}

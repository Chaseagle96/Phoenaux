import Foundation

enum ModelCheckFailure: Error {
    case expectation(String)
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    guard condition() else { throw ModelCheckFailure.expectation(message) }
}

@main
struct PhoenauxModelCheck {
    static func main() async throws {
        let iPhone = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.iPhoneSpeakerIdentifier
        )
        let airPods = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.airPodsPro3Identifier
        )
        try expect(iPhone.bassStrategy == .psychoacoustic, "iPhone profile must synthesize audible bass harmonics")
        try expect(airPods.bassStrategy == .extensionBass, "AirPods profile must favor fundamental extension")
        try expect(ProfileBassStrategy.extensionBass.rawValue == "extension", "profile JSON must retain its stable bass-strategy value")
        try expect(iPhone.ancestry.first == DeviceProfileCatalog.genericIdentifier, "profile inheritance must retain its root")

        let source = try PhoenauxPreset.reborn.document.validated()
        let data = try JSONEncoder().encode(source)
        let decoded = try JSONDecoder().decode(PhoenauxPresetDocument.self, from: data).validated()
        try expect(decoded == source, "preset JSON must round-trip without losing known fields")

        let advanced = try source.applyingAdvancedOverrides(
            moduleEnabled: [.crystalizer: false],
            parameters: [.inputGainDB: -99, .bassAmount: 0.9, .stereoWidth: 99]
        ).validated()
        try expect(advanced.module(.crystalizer)?.enabled == false,
            "advanced bypass must enter the effective preset")
        try expect(advanced.authoredValue(for: .inputGainDB) == -24,
            "advanced input trim must remain in its safe range")
        try expect(advanced.authoredValue(for: .stereoWidth) == 1.5,
            "advanced width must clamp before serialization")

        let half = try DSPStateCompiler.compile(preset: source, profile: iPhone, intensity: 0.5)
        let full = try DSPStateCompiler.compile(preset: source, profile: iPhone, intensity: 1)
        let headphones = try DSPStateCompiler.compile(preset: source, profile: airPods, intensity: 1)
        let advancedSpeaker = try DSPStateCompiler.compile(preset: advanced, profile: iPhone, intensity: 1)
        try expect(abs(half.bassAmount / full.bassAmount - 0.5) > 0.01, "bass intensity must be nonlinear")
        try expect(abs(half.crystalizerAmount / full.crystalizerAmount - 0.5) > 0.01, "detail intensity must be nonlinear")
        try expect(full.stereoWidth <= iPhone.maximumStereoWidth, "compiled width must obey the device cap")
        try expect(full.highPassFrequency != headphones.highPassFrequency, "profiles must compile distinct protection filters")
        try expect(!advancedSpeaker.crystalizerEnabled, "advanced bypass must compile into render state")
        try expect(advancedSpeaker.stereoWidth <= iPhone.maximumStereoWidth,
            "device width caps must override advanced authored width")

        let root = FileManager.default.temporaryDirectory
            .appending(path: UUID().uuidString, directoryHint: .isDirectory)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = PresetStore(directory: root.appending(path: "source"))
        let saved = try await store.save(source)
        let loaded = try await store.loadAll()
        try expect(loaded == [saved], "preset store must reload atomic JSON")

        let exportURL = root.appending(path: "Reborn.json")
        try await store.exportData(saved).write(to: exportURL, options: .atomic)
        let importStore = PresetStore(directory: root.appending(path: "imported"))
        let imported = try await importStore.importPreset(from: exportURL)
        let importedPresets = try await importStore.loadAll()
        try expect(imported == saved, "preset import must preserve validated content")
        try expect(importedPresets == [saved], "imported preset must persist atomically")

        try await store.delete(identifier: saved.identifier)
        let afterDelete = try await store.loadAll()
        try expect(afterDelete.isEmpty, "preset store deletion must target one validated identifier")

        print("Phoenaux profile and preset checks passed")
    }
}

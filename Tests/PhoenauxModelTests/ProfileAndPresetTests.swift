import Foundation
import XCTest
@testable import Phoenaux

final class ProfileAndPresetTests: XCTestCase {
    func testIPhoneProfileUsesProtectedPsychoacousticBass() throws {
        let profile = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.iPhoneSpeakerIdentifier
        )
        XCTAssertEqual(profile.bassStrategy, .psychoacoustic)
        XCTAssertGreaterThanOrEqual(profile.highPassFrequency, 50)
        XCTAssertLessThanOrEqual(profile.maximumStereoWidth, 1.1)
        XCTAssertEqual(profile.ancestry.first, DeviceProfileCatalog.genericIdentifier)
    }

    func testAirPodsPro3ProfileInheritsConservativeFamilyTuning() throws {
        let profile = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.airPodsPro3Identifier
        )
        XCTAssertEqual(profile.bassStrategy, .extensionBass)
        XCTAssertEqual(profile.bassStrategy.rawValue, "extension")
        XCTAssertEqual(profile.routeFamily, .airPodsPro3)
        XCTAssertTrue(profile.ancestry.contains("com.phoenaux.profile.airpods-pro"))
        XCTAssertLessThan(profile.bassAmountScale, 1)
    }

    func testIntensityCompilationIsDeviceFirstAndNonlinear() throws {
        let iPhone = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.iPhoneSpeakerIdentifier
        )
        let airPods = try DeviceProfileCatalog.resolve(
            identifier: DeviceProfileCatalog.airPodsPro3Identifier
        )
        let preset = PhoenauxPreset.reborn.document
        let half = try DSPStateCompiler.compile(preset: preset, profile: iPhone, intensity: 0.5)
        let full = try DSPStateCompiler.compile(preset: preset, profile: iPhone, intensity: 1)
        let headphones = try DSPStateCompiler.compile(preset: preset, profile: airPods, intensity: 1)

        XCTAssertNotEqual(half.bassAmount / full.bassAmount, 0.5, accuracy: 0.01)
        XCTAssertNotEqual(half.crystalizerAmount / full.crystalizerAmount, 0.5, accuracy: 0.01)
        XCTAssertLessThanOrEqual(full.stereoWidth, iPhone.maximumStereoWidth)
        XCTAssertEqual(full.bassStrategy, .psychoacoustic)
        XCTAssertEqual(headphones.bassStrategy, .extensionBass)
        XCTAssertNotEqual(full.highPassFrequency, headphones.highPassFrequency)
    }

    func testPresetRoundTripCapturesEveryModule() throws {
        let source = try PhoenauxPreset.crystal.document.validated()
        let data = try JSONEncoder().encode(source)
        let decoded = try JSONDecoder().decode(PhoenauxPresetDocument.self, from: data).validated()
        XCTAssertEqual(decoded, source)
        XCTAssertEqual(Set(decoded.graphOrder), Set(DSPModuleKind.allCases))
        XCTAssertEqual(Set(decoded.modules.map(\.kind)), Set(DSPModuleKind.allCases))
    }

    func testPresetRejectsDuplicateGraphModules() {
        var preset = PhoenauxPreset.reborn.document
        preset.graphOrder[0] = .limiter
        XCTAssertThrowsError(try preset.validated())
    }

    func testPresetStoreWritesAndReloadsAtomicJSON() async throws {
        let root = FileManager.default.temporaryDirectory
            .appending(path: UUID().uuidString, directoryHint: .isDirectory)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = PresetStore(directory: root.appending(path: "source"))
        let base = PhoenauxPreset.impact.document
        let source = PhoenauxPresetDocument(
            schemaVersion: base.schemaVersion,
            identifier: "user.phoenaux.\(UUID().uuidString.lowercased())",
            revision: 1,
            name: "Impact Test",
            profileCompatibility: [DeviceProfileCatalog.iPhoneSpeakerIdentifier],
            graphOrder: base.graphOrder,
            modules: base.modules,
            inputGainDB: base.inputGainDB,
            outputGainDB: base.outputGainDB,
            authoredIntensity: 0.8
        )
        let saved = try await store.save(source)
        let loaded = try await store.loadAll()
        XCTAssertEqual(loaded, [saved])

        let exportURL = root.appending(path: "Impact-Test.json")
        try await store.exportData(saved).write(to: exportURL, options: .atomic)
        let importStore = PresetStore(directory: root.appending(path: "imported"))
        let imported = try await importStore.importPreset(from: exportURL)
        let importedPresets = try await importStore.loadAll()
        XCTAssertEqual(imported, saved)
        XCTAssertEqual(importedPresets, [saved])

        try await store.delete(identifier: saved.identifier)
        let afterDelete = try await store.loadAll()
        XCTAssertEqual(afterDelete, [])
    }
}

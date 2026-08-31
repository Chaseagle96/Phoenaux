import Foundation

actor PresetStore {
    private let directory: URL
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    init(directory: URL? = nil) {
        let applicationSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory
        self.directory = directory ?? applicationSupport
            .appending(path: "Phoenaux", directoryHint: .isDirectory)
            .appending(path: "Presets", directoryHint: .isDirectory)
        encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        decoder = JSONDecoder()
    }

    func save(_ source: PhoenauxPresetDocument) throws -> PhoenauxPresetDocument {
        let preset = try source.validated()
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let data = try encoder.encode(preset)
        var writingOptions: Data.WritingOptions = [.atomic]
#if canImport(Darwin)
        writingOptions.insert(.completeFileProtection)
#endif
        try data.write(to: fileURL(for: preset.identifier), options: writingOptions)
        return preset
    }

    func loadAll() throws -> [PhoenauxPresetDocument] {
        guard FileManager.default.fileExists(atPath: directory.path()) else { return [] }
        let urls = try FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]
        ).filter { $0.pathExtension.lowercased() == "json" }
        return try urls.map { url in
            try decoder.decode(PhoenauxPresetDocument.self, from: Data(contentsOf: url)).validated()
        }.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    func importPreset(from url: URL) throws -> PhoenauxPresetDocument {
#if canImport(Darwin)
        let accessedSecurityScope = url.startAccessingSecurityScopedResource()
        defer {
            if accessedSecurityScope {
                url.stopAccessingSecurityScopedResource()
            }
        }
#endif
        let preset = try decoder.decode(PhoenauxPresetDocument.self, from: Data(contentsOf: url)).validated()
        return try save(preset)
    }

    func exportData(_ preset: PhoenauxPresetDocument) throws -> Data {
        try encoder.encode(preset.validated())
    }

    func delete(identifier: String) throws {
        _ = try PhoenauxPresetDocument(
            schemaVersion: 1,
            identifier: identifier,
            revision: 1,
            name: "Validation",
            profileCompatibility: [],
            graphOrder: DSPModuleKind.allCases,
            modules: DSPModuleKind.allCases.map { .init(kind: $0, enabled: false, parameters: [:]) },
            inputGainDB: 0,
            outputGainDB: 0,
            authoredIntensity: 0
        ).validated()
        let url = fileURL(for: identifier)
        if FileManager.default.fileExists(atPath: url.path()) {
            try FileManager.default.removeItem(at: url)
        }
    }

    private func fileURL(for identifier: String) -> URL {
        directory.appending(path: identifier).appendingPathExtension("json")
    }
}

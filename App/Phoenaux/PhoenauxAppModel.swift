import Foundation
import Observation

enum PhoenauxPresetSelection: Hashable, Sendable {
    case builtIn(PhoenauxPreset)
    case user(String)

    init?(persistenceValue: String) {
        if persistenceValue.hasPrefix("user:") {
            self = .user(String(persistenceValue.dropFirst("user:".count)))
        } else if persistenceValue.hasPrefix("builtIn:") {
            guard let preset = PhoenauxPreset(
                rawValue: String(persistenceValue.dropFirst("builtIn:".count))
            ) else { return nil }
            self = .builtIn(preset)
        } else if let preset = PhoenauxPreset(rawValue: persistenceValue) {
            self = .builtIn(preset)
        } else {
            return nil
        }
    }

    var persistenceValue: String {
        switch self {
        case let .builtIn(preset): "builtIn:\(preset.rawValue)"
        case let .user(identifier): "user:\(identifier)"
        }
    }
}

@Observable
@MainActor
final class PhoenauxAppModel {
    let routeMonitor: OutputRouteMonitor
    let audio: HostedAudioEngine

    private let presetStore: PresetStore
    private let defaults: UserDefaults
    private var lastCompiledState: CompiledDSPState?
    private var resumeAfterMediaServicesReset = false

    private(set) var activeProfile: ResolvedDeviceProfile
    private(set) var userPresets: [PhoenauxPresetDocument] = []
    private(set) var statusMessage: String?
    private(set) var moduleEnableOverrides: [DSPModuleKind: Bool] = [:]
    private(set) var advancedParameterOverrides: [AdvancedDSPParameter: Float] = [:]

    var isRebornEnabled: Bool {
        didSet {
            defaults.set(isRebornEnabled, forKey: "reborn.enabled")
            synchronizeDSP()
        }
    }
    private(set) var presetSelection: PhoenauxPresetSelection {
        didSet {
            guard oldValue != presetSelection else { return }
            moduleEnableOverrides = [:]
            advancedParameterOverrides = [:]
            defaults.set(presetSelection.persistenceValue, forKey: "preset.selected")
            synchronizeDSP()
        }
    }
    var intensity: Double {
        didSet {
            intensity = max(0, min(intensity, 1))
            defaults.set(intensity, forKey: "reborn.intensity")
            synchronizeDSP()
        }
    }

    init(
        routeMonitor: OutputRouteMonitor = OutputRouteMonitor(),
        audio: HostedAudioEngine = HostedAudioEngine(),
        presetStore: PresetStore = PresetStore(),
        defaults: UserDefaults = .standard
    ) {
        self.routeMonitor = routeMonitor
        self.audio = audio
        self.presetStore = presetStore
        self.defaults = defaults
        activeProfile = DeviceProfileCatalog.resolve(for: routeMonitor.route)
        isRebornEnabled = defaults.object(forKey: "reborn.enabled") as? Bool ?? true
        presetSelection = defaults.string(forKey: "preset.selected")
            .flatMap(PhoenauxPresetSelection.init(persistenceValue:)) ?? .builtIn(.reborn)
        intensity = defaults.object(forKey: "reborn.intensity") as? Double ?? 0.78

        routeMonitor.onRouteChange = { [weak self] route in
            self?.handleRouteChange(route)
        }
        routeMonitor.onMediaServicesReset = { [weak self] in
            self?.handleMediaServicesReset()
        }
        routeMonitor.onMediaServicesLost = { [weak self] in
            self?.handleMediaServicesLost()
        }
        Task { [weak self, presetStore] in
            do {
                let presets = try await presetStore.loadAll()
                guard let self else { return }
                userPresets = presets
                if case let .user(identifier) = presetSelection,
                   !presets.contains(where: { $0.identifier == identifier }) {
                    presetSelection = .builtIn(.reborn)
                    statusMessage = "The previously selected saved preset is unavailable; Reborn was restored."
                }
            } catch {
                self?.statusMessage = "Saved presets could not be loaded: \(error.localizedDescription)"
            }
        }
    }

    var activePresetName: String {
        activePresetDocument?.name ?? "Unavailable Preset"
    }

    var activePresetPurpose: String {
        switch presetSelection {
        case let .builtIn(preset): preset.purpose
        case .user: activePresetDocument?.notes ?? "Saved Phoenaux tuning snapshot"
        }
    }

    var activePresetExportFilename: String {
        let safeName = activePresetName
            .components(separatedBy: CharacterSet.alphanumerics.inverted)
            .filter { !$0.isEmpty }
            .joined(separator: "-")
        return safeName.isEmpty ? "Phoenaux-Preset" : safeName
    }

    var hasAdvancedOverrides: Bool {
        !moduleEnableOverrides.isEmpty || !advancedParameterOverrides.isEmpty
    }

    func moduleEnabled(_ kind: DSPModuleKind) -> Bool {
        moduleEnableOverrides[kind] ?? activePresetDocument?.module(kind)?.enabled ?? false
    }

    func setModuleEnabled(_ kind: DSPModuleKind, enabled: Bool) {
        guard let authored = activePresetDocument?.module(kind)?.enabled else { return }
        if enabled == authored {
            moduleEnableOverrides.removeValue(forKey: kind)
        } else {
            moduleEnableOverrides[kind] = enabled
        }
        synchronizeDSP()
    }

    func advancedValue(for parameter: AdvancedDSPParameter) -> Float {
        advancedParameterOverrides[parameter]
            ?? activePresetDocument?.authoredValue(for: parameter)
            ?? parameter.defaultValue
    }

    func setAdvancedValue(_ value: Float, for parameter: AdvancedDSPParameter) {
        let clamped = parameter.clamped(value)
        let authored = activePresetDocument?.authoredValue(for: parameter)
            ?? parameter.defaultValue
        if clamped == authored {
            advancedParameterOverrides.removeValue(forKey: parameter)
        } else {
            advancedParameterOverrides[parameter] = clamped
        }
        synchronizeDSP()
    }

    func resetAdvancedOverrides() {
        guard hasAdvancedOverrides else { return }
        moduleEnableOverrides = [:]
        advancedParameterOverrides = [:]
        synchronizeDSP()
        statusMessage = "Advanced adjustments reset to \(activePresetName)."
    }

    func selectPreset(_ selection: PhoenauxPresetSelection) {
        guard let preset = document(for: selection) else {
            statusMessage = "That saved preset is no longer available."
            return
        }
        do {
            _ = try compile(preset)
            presetSelection = selection
            statusMessage = "Applied \(preset.name)."
        } catch {
            statusMessage = "Preset could not be applied: \(error.localizedDescription)"
        }
    }

    func togglePlayback() {
        if audio.isPlaying {
            audio.stop()
            return
        }
        routeMonitor.refresh()
        do {
            let state = try compileCurrentState()
            lastCompiledState = state
            audio.startSelectedSource(state: state, enabled: isRebornEnabled)
        } catch {
            statusMessage = error.localizedDescription
        }
    }

    func importAudioFile(_ url: URL) {
        Task { [weak self] in
            guard let self else { return }
            do {
                try await audio.loadLocalFile(from: url)
                statusMessage = "Ready to play “\(audio.loadedFileName ?? "audio file")”."
            } catch {
                statusMessage = "Audio file could not be loaded: \(error.localizedDescription)"
            }
        }
    }

    func reportAudioImportFailure(_ error: Error) {
        statusMessage = "Audio file could not be selected: \(error.localizedDescription)"
    }

    func useGeneratedSignal() {
        audio.useGeneratedSignal()
        statusMessage = "Using the built-in DSP test tone."
    }

    func importPreset(_ url: URL) {
        Task { [weak self, presetStore] in
            do {
                let imported = try await presetStore.importPreset(from: url)
                guard let self else { return }
                userPresets.removeAll { $0.identifier == imported.identifier }
                userPresets.append(imported)
                userPresets.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
                do {
                    _ = try compile(imported)
                    let selection = PhoenauxPresetSelection.user(imported.identifier)
                    if presetSelection == selection {
                        moduleEnableOverrides = [:]
                        advancedParameterOverrides = [:]
                        synchronizeDSP()
                    } else {
                        presetSelection = selection
                    }
                    statusMessage = "Imported and applied “\(imported.name)”."
                } catch {
                    statusMessage = "Imported “\(imported.name)”, but this build cannot apply it: \(error.localizedDescription)"
                }
            } catch {
                self?.statusMessage = "Preset could not be imported: \(error.localizedDescription)"
            }
        }
    }

    func exportSelectedPreset() async -> Data? {
        guard let preset = effectivePresetDocument else {
            statusMessage = "The selected preset is unavailable."
            return nil
        }
        do {
            return try await presetStore.exportData(preset)
        } catch {
            statusMessage = "Preset could not be exported: \(error.localizedDescription)"
            return nil
        }
    }

    func reportPresetImportFailure(_ error: Error) {
        statusMessage = "Preset could not be selected: \(error.localizedDescription)"
    }

    func reportPresetExportResult(_ result: Result<URL, Error>) {
        switch result {
        case .success: statusMessage = "Preset exported."
        case let .failure(error):
            statusMessage = "Preset could not be exported: \(error.localizedDescription)"
        }
    }

    func saveCurrentPreset(named proposedName: String? = nil) {
        guard let base = effectivePresetDocument else {
            statusMessage = "The selected preset is unavailable."
            return
        }
        let name = proposedName?.trimmingCharacters(in: .whitespacesAndNewlines)
        let resolvedName = (name?.isEmpty == false ? name : nil) ?? "\(base.name) Snapshot"
        let snapshot = PhoenauxPresetDocument(
            schemaVersion: PhoenauxPresetDocument.currentSchemaVersion,
            identifier: "user.phoenaux.\(UUID().uuidString.lowercased())",
            revision: 1,
            name: resolvedName,
            profileCompatibility: activeProfile.ancestry,
            graphOrder: base.graphOrder,
            modules: base.modules,
            inputGainDB: base.inputGainDB,
            outputGainDB: base.outputGainDB,
            authoredIntensity: Float(intensity),
            notes: "Saved from \(activeProfile.displayName), revision \(activeProfile.revision)."
        )
        Task { [weak self, presetStore] in
            do {
                let saved = try await presetStore.save(snapshot)
                guard let self else { return }
                userPresets.removeAll { $0.identifier == saved.identifier }
                userPresets.append(saved)
                userPresets.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
                statusMessage = "Saved preset “\(saved.name)”."
            } catch {
                self?.statusMessage = "Preset could not be saved: \(error.localizedDescription)"
            }
        }
    }

    private func handleRouteChange(_ route: PhoenauxOutputRoute) {
        activeProfile = DeviceProfileCatalog.resolve(for: route)
        do {
            let state = try compileCurrentState()
            lastCompiledState = state
            if audio.isPlaying {
                audio.restartSelectedSource(state: state, enabled: isRebornEnabled)
            } else {
                audio.apply(state: state, enabled: isRebornEnabled)
            }
            statusMessage = "Applied \(activeProfile.displayName)."
        } catch {
            statusMessage = error.localizedDescription
        }
    }

    private func handleMediaServicesReset() {
        activeProfile = DeviceProfileCatalog.resolve(for: routeMonitor.route)
        let shouldResume = audio.isPlaying || resumeAfterMediaServicesReset
        resumeAfterMediaServicesReset = false
        do {
            let state = try compileCurrentState()
            lastCompiledState = state
            if shouldResume {
                audio.stop()
                audio.startSelectedSource(state: state, enabled: isRebornEnabled)
            } else {
                audio.apply(state: state, enabled: isRebornEnabled)
            }
            statusMessage = "Audio services recovered with \(activeProfile.displayName)."
        } catch {
            statusMessage = "Audio services reset, but Phoenaux could not recover: \(error.localizedDescription)"
        }
    }

    private func handleMediaServicesLost() {
        resumeAfterMediaServicesReset = audio.isPlaying
        audio.stop()
        statusMessage = "Playback paused because iOS audio services became unavailable."
    }

    private func synchronizeDSP() {
        do {
            let state = try compileCurrentState()
            let latencyChanged = lastCompiledState?.limiterLookaheadMilliseconds
                != state.limiterLookaheadMilliseconds
            lastCompiledState = state
            if audio.isPlaying && latencyChanged {
                audio.restartSelectedSource(state: state, enabled: isRebornEnabled)
            } else {
                audio.apply(state: state, enabled: isRebornEnabled)
            }
        } catch {
            statusMessage = error.localizedDescription
        }
    }

    private func compileCurrentState() throws -> CompiledDSPState {
        guard let preset = effectivePresetDocument else {
            throw PresetDocumentError.invalidIdentifier
        }
        return try compile(preset)
    }

    private func compile(_ preset: PhoenauxPresetDocument) throws -> CompiledDSPState {
        try DSPStateCompiler.compile(
            preset: preset,
            profile: activeProfile,
            intensity: intensity
        )
    }

    private var activePresetDocument: PhoenauxPresetDocument? {
        document(for: presetSelection)
    }

    private var effectivePresetDocument: PhoenauxPresetDocument? {
        activePresetDocument?.applyingAdvancedOverrides(
            moduleEnabled: moduleEnableOverrides,
            parameters: advancedParameterOverrides
        )
    }

    private func document(for selection: PhoenauxPresetSelection) -> PhoenauxPresetDocument? {
        switch selection {
        case let .builtIn(preset): preset.document
        case let .user(identifier): userPresets.first { $0.identifier == identifier }
        }
    }
}

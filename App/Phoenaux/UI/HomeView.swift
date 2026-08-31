import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct HomeView: View {
    @Bindable var model: PhoenauxAppModel
    @State private var isImportingAudio = false
    @State private var isImportingPreset = false
    @State private var isExportingPreset = false
    @State private var presetExportDocument: PresetExportDocument?

    var body: some View {
        NavigationStack {
            ZStack {
                PhoenauxBackdrop()
                ScrollView {
                    VStack(spacing: 18) {
                        brand
                        routeCard
                        sourceCard
                        rebornCard
                        presetCard
                        metersCard
                        playbackButton
                        status
                    }
                    .padding(.horizontal, 20)
                    .padding(.vertical, 18)
                    .frame(maxWidth: 680)
                    .frame(maxWidth: .infinity)
                }
            }
            .toolbar(.hidden, for: .navigationBar)
        }
        .preferredColorScheme(.dark)
        .fileImporter(
            isPresented: $isImportingAudio,
            allowedContentTypes: [.audio]
        ) { result in
            switch result {
            case let .success(url): model.importAudioFile(url)
            case let .failure(error): model.reportAudioImportFailure(error)
            }
        }
        .fileImporter(
            isPresented: $isImportingPreset,
            allowedContentTypes: [.json]
        ) { result in
            switch result {
            case let .success(url): model.importPreset(url)
            case let .failure(error): model.reportPresetImportFailure(error)
            }
        }
        .fileExporter(
            isPresented: $isExportingPreset,
            document: presetExportDocument,
            contentType: .json,
            defaultFilename: model.activePresetExportFilename,
            onCompletion: model.reportPresetExportResult
        )
    }

    private var sourceCard: some View {
        PhoenauxCard {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("AUDIO SOURCE")
                            .font(.caption2.weight(.bold))
                            .tracking(1.2)
                            .foregroundStyle(.secondary)
                        Text(sourceTitle)
                            .font(.headline)
                            .lineLimit(1)
                        Text(sourceSubtitle)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    if model.audio.isLoadingFile {
                        ProgressView()
                            .accessibilityLabel("Loading audio file")
                    } else {
                        Image(systemName: model.audio.sourceKind == .localFile
                            ? "music.note" : "waveform")
                            .foregroundStyle(.orange)
                            .accessibilityHidden(true)
                    }
                }

                if model.audio.sourceKind == .localFile {
                    ProgressView(value: model.audio.playbackProgress)
                        .tint(.orange)
                        .accessibilityLabel("Playback progress")
                        .accessibilityValue("\(Int(model.audio.playbackProgress * 100)) percent")
                }

                HStack {
                    Button("Choose Audio File", systemImage: "folder") {
                        isImportingAudio = true
                    }
                    .disabled(model.audio.isLoadingFile)
                    .accessibilityHint("Selects a mono or stereo audio file for hosted playback")

                    Spacer()

                    if model.audio.sourceKind == .localFile {
                        Button("Use Test Tone", systemImage: "waveform") {
                            model.useGeneratedSignal()
                        }
                    }
                }
                .font(.caption.weight(.semibold))
            }
        }
    }

    private var brand: some View {
        VStack(spacing: 4) {
            Image(systemName: "waveform.path.ecg.rectangle.fill")
                .font(.system(size: 34, weight: .semibold))
                .foregroundStyle(.orange.gradient)
                .accessibilityHidden(true)
            Text("Phoenaux")
                .font(.system(.largeTitle, design: .rounded, weight: .bold))
            Text("Audio, Reborn.")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 8)
    }

    private var routeCard: some View {
        PhoenauxCard {
            HStack(spacing: 14) {
                Image(systemName: routeSymbol)
                    .font(.title2)
                    .frame(width: 42, height: 42)
                    .background(.orange.opacity(0.16), in: .circle)
                VStack(alignment: .leading, spacing: 3) {
                    Text("CURRENT OUTPUT")
                        .font(.caption2.weight(.bold))
                        .tracking(1.2)
                        .foregroundStyle(.secondary)
                    Text(model.routeMonitor.route.displayName)
                        .font(.headline)
                    Text("\(model.activeProfile.displayName) • Auto")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                if model.routeMonitor.sampleRate > 0 {
                    Text("\(model.routeMonitor.sampleRate / 1_000, specifier: "%.1f") kHz")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
        }
        .accessibilityElement(children: .combine)
    }

    private var rebornCard: some View {
        PhoenauxCard(tint: .orange.opacity(model.isRebornEnabled ? 0.18 : 0.04)) {
            VStack(spacing: 18) {
                HStack {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("REBORN")
                            .font(.title2.weight(.black))
                            .tracking(1.4)
                        Text(model.isRebornEnabled ? "Phoenaux processing active" : "Natural signal")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    Toggle("Reborn processing", isOn: $model.isRebornEnabled)
                        .labelsHidden()
                        .tint(.orange)
                }

                VStack(spacing: 8) {
                    HStack {
                        Text("Natural")
                        Spacer()
                        Text("Reborn")
                    }
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                    Slider(value: $model.intensity, in: 0...1)
                        .tint(.orange)
                        .accessibilityLabel("Reborn intensity")
                        .accessibilityValue("\(Int(model.intensity * 100)) percent")
                }
                .disabled(!model.isRebornEnabled)
            }
        }
    }

    private var presetCard: some View {
        PhoenauxCard {
            VStack(alignment: .leading, spacing: 12) {
                Text("PRESET")
                    .font(.caption2.weight(.bold))
                    .tracking(1.2)
                    .foregroundStyle(.secondary)
                Picker(
                    "Preset",
                    selection: Binding(
                        get: { model.presetSelection },
                        set: model.selectPreset
                    )
                ) {
                    Section("Built-in") {
                        ForEach(PhoenauxPreset.allCases) { preset in
                            Text(preset.rawValue)
                                .tag(PhoenauxPresetSelection.builtIn(preset))
                        }
                    }
                    if !model.userPresets.isEmpty {
                        Section("Saved") {
                            ForEach(model.userPresets) { preset in
                                Text(preset.name)
                                    .tag(PhoenauxPresetSelection.user(preset.identifier))
                            }
                        }
                    }
                }
                .pickerStyle(.menu)
                .tint(.primary)
                Text(model.activePresetPurpose)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                LazyVGrid(columns: [.init(.adaptive(minimum: 92), spacing: 7)], spacing: 7) {
                    ForEach(
                        ["Filter", "EQ", "Bass", "Exciter", "Crystalizer", "Stereo", "Limiter"],
                        id: \.self
                    ) { module in
                        Label(module, systemImage: "checkmark.circle.fill")
                            .font(.caption2.weight(.semibold))
                            .foregroundStyle(.green)
                    }
                }
                .accessibilityElement(children: .combine)
                HStack {
                    Button("Save Snapshot", systemImage: "square.and.arrow.down") {
                        model.saveCurrentPreset()
                    }
                    .font(.caption.weight(.semibold))
                    Spacer()
                    if !model.userPresets.isEmpty {
                        Text("\(model.userPresets.count) saved")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }
                HStack(spacing: 18) {
                    Button("Import", systemImage: "square.and.arrow.down") {
                        isImportingPreset = true
                    }
                    .accessibilityHint("Imports and validates a Phoenaux preset JSON file")

                    Button("Export", systemImage: "square.and.arrow.up") {
                        Task {
                            guard let data = await model.exportSelectedPreset() else { return }
                            presetExportDocument = PresetExportDocument(data: data)
                            isExportingPreset = true
                        }
                    }
                    .accessibilityHint("Exports the selected preset as a JSON file")
                }
                .font(.caption.weight(.semibold))
            }
        }
    }

    private var metersCard: some View {
        PhoenauxCard {
            VStack(spacing: 12) {
                MeterRow(title: "Input", value: model.audio.inputPeak)
                MeterRow(title: "Output", value: model.audio.outputPeak)
                HStack {
                    Text("Limiter reduction")
                    Spacer()
                    Text("\(model.audio.gainReductionDB, specifier: "%.1f") dB")
                        .monospacedDigit()
                }
                .font(.caption)
                .foregroundStyle(.secondary)
            }
        }
    }

    private var playbackButton: some View {
        Button(action: model.togglePlayback) {
            Label(
                playbackButtonTitle,
                systemImage: model.audio.isPlaying ? "stop.fill" : "play.fill"
            )
            .font(.headline)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 8)
        }
        .buttonStyle(.phoenauxProminentGlass)
        .disabled(model.audio.isLoadingFile)
        .accessibilityHint(playbackAccessibilityHint)
    }

    @ViewBuilder
    private var status: some View {
        if let error = model.audio.lastError {
            Label(error, systemImage: "exclamationmark.triangle.fill")
                .font(.caption)
                .foregroundStyle(.yellow)
                .multilineTextAlignment(.center)
                .accessibilityLabel("Audio error: \(error)")
        } else if let message = model.statusMessage {
            Text(message)
                .font(.caption2)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        } else {
            Text("Hosted audio only • Other apps are not processed")
                .font(.caption2)
                .foregroundStyle(.tertiary)
        }
    }

    private var routeSymbol: String {
        switch model.routeMonitor.route {
        case .iPhoneSpeaker: "iphone.gen3.radiowaves.left.and.right"
        case .airPodsPro3, .airPodsProFamily, .bluetoothHeadphones, .wiredHeadphones: "headphones"
        case .bluetoothSpeaker: "hifispeaker.fill"
        case .usbAudio: "cable.connector"
        case .airPlay: "airplayaudio"
        case .unknown: "speaker.wave.2.fill"
        }
    }

    private var sourceTitle: String {
        model.audio.loadedFileName ?? "Built-in Test Tone"
    }

    private var sourceSubtitle: String {
        guard model.audio.sourceKind == .localFile else {
            return "220 Hz tone with harmonic content"
        }
        return "Local file • \(formattedDuration(model.audio.loadedFileDuration)) • Hosted playback"
    }

    private var playbackButtonTitle: String {
        if model.audio.isPlaying {
            return "Stop Playback"
        }
        return model.audio.sourceKind == .localFile ? "Play Audio File" : "Play DSP Demo"
    }

    private var playbackAccessibilityHint: String {
        model.audio.sourceKind == .localFile
            ? "Plays the selected file through the hosted PhoenauxDSP chain"
            : "Plays a generated test tone through the hosted PhoenauxDSP chain"
    }

    private func formattedDuration(_ duration: TimeInterval) -> String {
        let seconds = max(0, Int(duration.rounded()))
        return String(format: "%d:%02d", seconds / 60, seconds % 60)
    }
}

private struct PhoenauxBackdrop: View {
    var body: some View {
        LinearGradient(
            colors: [Color(red: 0.055, green: 0.055, blue: 0.075), .black],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .overlay(alignment: .topTrailing) {
            Circle()
                .fill(.orange.opacity(0.16))
                .frame(width: 300, height: 300)
                .blur(radius: 80)
                .offset(x: 100, y: -100)
        }
        .ignoresSafeArea()
    }
}

private struct PhoenauxCard<Content: View>: View {
    var tint: Color = .white.opacity(0.04)
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .phoenauxGlass(tint: tint)
    }
}

private struct MeterRow: View {
    let title: String
    let value: Float

    private var normalized: Double {
        min(1, max(0, Double(value)))
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                Text(title)
                Spacer()
                Text("\(20 * log10(max(Double(value), 0.000_001)), specifier: "%.1f") dBFS")
                    .monospacedDigit()
            }
            .font(.caption)
            .foregroundStyle(.secondary)
            ProgressView(value: normalized)
                .tint(normalized > 0.9 ? .orange : .green)
                .accessibilityLabel("\(title) level")
        }
    }
}

private extension View {
    @ViewBuilder
    func phoenauxGlass(tint: Color) -> some View {
        if #available(iOS 26.0, *) {
            glassEffect(.regular.tint(tint), in: .rect(cornerRadius: 24))
        } else {
            background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24, style: .continuous))
                .overlay {
                    RoundedRectangle(cornerRadius: 24, style: .continuous)
                        .stroke(.white.opacity(0.08), lineWidth: 1)
                }
        }
    }
}

private struct PhoenauxProminentGlassButtonStyle: PrimitiveButtonStyle {
    @ViewBuilder
    func makeBody(configuration: Configuration) -> some View {
        if #available(iOS 26.0, *) {
            Button(configuration)
                .buttonStyle(.glassProminent)
                .tint(.orange)
        } else {
            Button(configuration)
                .buttonStyle(.borderedProminent)
                .tint(.orange)
        }
    }
}

private extension PrimitiveButtonStyle where Self == PhoenauxProminentGlassButtonStyle {
    static var phoenauxProminentGlass: PhoenauxProminentGlassButtonStyle { .init() }
}

import AVFAudio
import Foundation
import Observation

enum PhoenauxOutputRoute: Equatable, Sendable {
    case iPhoneSpeaker
    case airPodsPro3
    case airPodsProFamily
    case bluetoothHeadphones
    case bluetoothSpeaker
    case wiredHeadphones
    case usbAudio
    case airPlay
    case unknown(name: String)

    var displayName: String {
        switch self {
        case .iPhoneSpeaker: "iPhone Speaker"
        case .airPodsPro3: "AirPods Pro 3"
        case .airPodsProFamily: "AirPods Pro"
        case .bluetoothHeadphones: "Bluetooth Headphones"
        case .bluetoothSpeaker: "Bluetooth Speaker"
        case .wiredHeadphones: "Wired Headphones"
        case .usbAudio: "USB Audio"
        case .airPlay: "AirPlay"
        case let .unknown(name): name
        }
    }

}

@Observable
@MainActor
final class OutputRouteMonitor: NSObject {
    private(set) var route: PhoenauxOutputRoute = .unknown(name: "No active output")
    private(set) var sampleRate: Double = 0
    var onRouteChange: (@MainActor (PhoenauxOutputRoute) -> Void)?
    var onMediaServicesLost: (@MainActor () -> Void)?
    var onMediaServicesReset: (@MainActor () -> Void)?

    override init() {
        super.init()
        refresh()
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(routeChanged),
            name: AVAudioSession.routeChangeNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(mediaServicesLost),
            name: AVAudioSession.mediaServicesWereLostNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(mediaServicesReset),
            name: AVAudioSession.mediaServicesWereResetNotification,
            object: nil
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    func refresh() {
        refresh(notify: true, forceNotification: false)
    }

    private func refresh(notify: Bool, forceNotification: Bool) {
        let session = AVAudioSession.sharedInstance()
        let previousSampleRate = sampleRate
        sampleRate = session.sampleRate
        let sampleRateChanged = previousSampleRate > 0
            && abs(previousSampleRate - sampleRate) > 0.5
        guard let output = session.currentRoute.outputs.first else {
            updateRoute(
                .unknown(name: "No active output"),
                notify: notify,
                forceNotification: forceNotification || sampleRateChanged
            )
            return
        }

        let normalizedName = output.portName.folding(
            options: [.caseInsensitive, .diacriticInsensitive],
            locale: .current
        ).lowercased()
        if normalizedName.contains("airpods pro 3") {
            updateRoute(
                .airPodsPro3,
                notify: notify,
                forceNotification: forceNotification || sampleRateChanged
            )
            return
        }
        if normalizedName.contains("airpods pro") {
            updateRoute(
                .airPodsProFamily,
                notify: notify,
                forceNotification: forceNotification || sampleRateChanged
            )
            return
        }

        let detectedRoute: PhoenauxOutputRoute
        switch output.portType {
        case .builtInSpeaker:
            detectedRoute = .iPhoneSpeaker
        case .headphones, .headsetMic:
            detectedRoute = .wiredHeadphones
        case .bluetoothA2DP, .bluetoothLE, .bluetoothHFP:
            detectedRoute = normalizedName.contains("speaker") ? .bluetoothSpeaker : .bluetoothHeadphones
        case .usbAudio:
            detectedRoute = .usbAudio
        case .airPlay:
            detectedRoute = .airPlay
        default:
            detectedRoute = .unknown(name: output.portName)
        }
        updateRoute(
            detectedRoute,
            notify: notify,
            forceNotification: forceNotification || sampleRateChanged
        )
    }

    private func updateRoute(
        _ newRoute: PhoenauxOutputRoute,
        notify: Bool,
        forceNotification: Bool
    ) {
        let routeChanged = route != newRoute
        route = newRoute
        if notify && (routeChanged || forceNotification) {
            onRouteChange?(newRoute)
        }
    }

    @objc private func routeChanged() {
        refresh(notify: true, forceNotification: false)
    }

    @objc private func mediaServicesReset() {
        refresh(notify: false, forceNotification: false)
        onMediaServicesReset?()
    }

    @objc private func mediaServicesLost() {
        onMediaServicesLost?()
    }
}

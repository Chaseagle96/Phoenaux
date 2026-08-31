#if os(Linux)
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
}
#endif

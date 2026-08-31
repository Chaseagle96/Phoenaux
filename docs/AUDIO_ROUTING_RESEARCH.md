# Audio Routing Research

Last reviewed: 2026-08-31. Target: iOS 27 beta-era documentation. Beta APIs and documentation can change before final release.

## Conclusion

The defensible production architecture is hosted playback plus an AUv3 effect. Apple documents app-scoped playback engines, host-loaded audio-unit extensions, and media-item taps; it does not document a public iPhone API that inserts a third-party processor into every other app's final output route. Phoenaux therefore will not advertise stock-iPhone system-wide processing.

Absence of a documented API is not treated as proof that every imaginable private technique is impossible. It is a boundary on what this project can promise, ship, and support with public APIs.

## Capability matrix

| Architecture | Other-app audio | Real time | AirPods output | Background | App Store | Sideload | Status and evidence |
|---|---:|---:|---:|---:|---:|---:|---|
| Phoenaux `AVAudioEngine` host | No | Yes | Yes, through the active route | Yes, while legitimately playing | Yes | Yes | **PROVEN API SHAPE.** Nodes and taps belong to an app's engine graph; the output node reaches that app's output route. Hardware validation pending. |
| Phoenaux AUv3 effect | Only when a host supplies it | Yes | Host-dependent | Host-dependent | Yes | Yes | **IMPLEMENTED; HOST VALIDATION PENDING.** The extension wraps the shared graph and exposes host parameters. Apple describes AUv3 effects as host-instantiated app extensions that run out of process on iOS. |
| `MTAudioProcessingTap` / `AVAudioMix` | No | Yes for eligible player media | Yes | Player-dependent | Yes | Yes | **LIMITED.** A tap attaches to a track in an `AVPlayerItem`; `audioMix` is file-media only and is not supported for HLS. |
| ReplayKit app capture | No; captures the calling app | Capture, not an output insert | Not a reinjection route | Recording-dependent | Yes for valid capture use | Yes | **LIMITED.** `startCapture` records the app's audio/video. It provides sample buffers but Apple documents no API that returns modified buffers to the live system output. |
| ReplayKit broadcast upload extension | Captured broadcast media, with user initiation | Streaming callback | Not a reinjection route | Broadcast lifecycle | Yes for valid broadcast use | Yes | **LIMITED.** Broadcast handlers consume sequential sample buffers for broadcasting. This is not documented as an audio output effect. |
| Network interception / local VPN | Encoded network traffic only | Not general PCM | No direct relation | Entitlement/configuration dependent | Restricted to valid networking use | Yes | **DISPROVEN AS GENERAL AUDIO ROUTING.** It cannot generally recover decoded DRM/local/app-generated PCM and provides no output insertion point. |
| AudioDriverKit | N/A on iPhone | Driver-level on supported systems | N/A on iPhone | Platform-dependent | N/A on iPhone | N/A on iPhone | **BLOCKED BY PLATFORM.** Apple lists AudioDriverKit for macOS and M-series iPadOS, not iOS on iPhone. It is for audio-device drivers, not an iPhone global-effect entitlement. |
| Private system service / jailbreak hook | Potentially | Unknown until prototyped | Unknown | Unknown | No | Environment-dependent | **PRIVATE API / JAILBREAK DEPENDENT.** Keep isolated; no production claims or implementation exists yet. |

“AirPods output” means Phoenaux-owned or host-supplied audio can leave through the system's active route. It does not mean Phoenaux can identify every AirPods model with guaranteed precision or bypass Apple's own headphone processing.

## Evidence notes

### AVAudioEngine and taps

Apple defines `AVAudioNode` instances as nodes attached to an `AVAudioEngine`. A node tap receives the output of a bus on that node. This supports hosted processing and observation inside Phoenaux's graph, not discovery of another process's graph.

- [AVAudioNode](https://developer.apple.com/documentation/avfaudio/avaudionode)
- [installAudioTap](https://developer.apple.com/documentation/avfaudio/avaudionode/installaudiotap(onbus:buffersize:format:tapprovider:))

### AUv3

Apple’s AUv3 sample has a host search for, instantiate, and connect extensions to its playback engine. It explicitly says iOS AUv3 plug-ins run out of process. Phoenaux can therefore be available in compatible hosts, but cannot force an arbitrary app to load it.

Phoenaux implements a headless `aufx` extension. Its render block pulls host input directly into the supplied output buffers and calls the prepared `PhoenauxDSP` C ABI in place. The first implementation accepts matching mono or stereo, noninterleaved Float32 I/O. Host automation is currently block-rate; sample-accurate `AURenderEvent` slicing remains validation work.

- [Incorporating Audio Effects and Instruments](https://developer.apple.com/documentation/audiotoolbox/incorporating-audio-effects-and-instruments)
- [Creating custom audio effects](https://developer.apple.com/documentation/avfaudio/creating-custom-audio-effects)

### AVFoundation media taps

`AVMutableAudioMixInputParameters.audioTapProcessor` associates a processing tap with a media track. `AVPlayerItem.audioMix` limits this route to file-based media and excludes HLS. It is valuable for a Phoenaux player, not global routing.

- [audioTapProcessor](https://developer.apple.com/documentation/avfoundation/avmutableaudiomixinputparameters/audiotapprocessor)
- [AVPlayerItem.audioMix](https://developer.apple.com/documentation/avfoundation/avplayeritem/audiomix)

### ReplayKit

ReplayKit is documented as recording or streaming the screen plus audio from the app and microphone. The callback supplies capture buffers. No corresponding public API accepts processed audio as a replacement for the system's live output.

- [ReplayKit](https://developer.apple.com/documentation/replaykit)
- [RPScreenRecorder.startCapture](https://developer.apple.com/documentation/replaykit/rpscreenrecorder/startcapture(handler:completionhandler:))
- [RPSampleBufferType](https://developer.apple.com/documentation/replaykit/rpsamplebuffertype)

### Driver boundary

Apple documents DriverKit on macOS and M-series iPadOS. AudioDriverKit follows the same supported platform boundary.

- [DriverKit](https://developer.apple.com/documentation/driverkit)
- [AudioDriverKit](https://developer.apple.com/documentation/audiodriverkit)

## Next experiments

1. Prove hosted `AVAudioEngine` rendering, route changes, interruptions, and background playback on an iOS 27 device.
2. Prove the shared engine inside an AUv3 host and measure out-of-process overhead.
3. Treat ReplayKit only as a capture-latency experiment. Success requires an authorized, documented return route; capture alone is not success.
4. Keep all private/sideload research in an independent target with explicit build flags and risk documentation.

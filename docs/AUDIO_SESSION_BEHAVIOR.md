# Audio Session Behavior

## Production hosted player

The hosted player uses `AVAudioSession.Category.playback` and activates only when playback begins. It currently accepts a generated test signal or a user-selected local mono/stereo file. Background audio is enabled because playback is the app's primary function, not as a generic keep-alive. Apple documents that the playback category continues through the silent switch and screen lock, and that background continuation additionally requires the Audio/AirPlay/Picture in Picture background mode.

Local files are decoded to non-interleaved float PCM on a detached task while their security-scoped URL is active. Playback begins only after the source is sealed. The decoded payload is capped at 256 MiB, and the audio callback performs no file I/O. Manual stop retains position, end-of-file stops transport, and a subsequent play after end resets to the beginning.

- [AVAudioSession](https://developer.apple.com/documentation/avfaudio/avaudiosession)
- [Configuring your app for media playback](https://developer.apple.com/documentation/avfoundation/configuring-your-app-for-media-playback)

## Events

The hosted adapter currently handles:

- interruption begin/end, resuming only when playback was active and iOS supplies `.shouldResume`;
- route-family and sample-rate changes through a stopped engine rebuild;
- media-services loss/reset through transport-intent capture, route re-resolution, DSP recompilation, and engine rebuild;
- local-source frame retention across interruption and rebuild.

Physical-device validation is still required for channel-count changes, Bluetooth reconnect/output replacement, calls, Siri, alarms, lock, and foreground/background transitions.

The current hosted adapter stops and rebuilds `AVAudioEngine` when a route/sample-rate change or limiter-latency change requires it, then resumes a local source from its retained frame position. A future graph-swap implementation can remove that transport restart. Route changes must not leave stale coefficients active.

## Route identity

`AVAudioSession.currentRoute.outputs` provides port type, a descriptive `portName`, and a system-assigned UID. This is adequate for broad route families and best-effort profile matching. Apple does not document `portName` as a stable product-model identifier, so exact AirPods Pro 3 matching needs hardware observation and a generic AirPods fallback.

- [AVAudioSessionPortDescription](https://developer.apple.com/documentation/avfaudio/avaudiosessionportdescription)
- [Responding to audio route changes](https://developer.apple.com/documentation/avfaudio/responding-to-audio-route-changes)

## Policy

Phoenaux will not use silent playback, background tasks, or repeated session activation as an abusive keep-alive. An AUv3 extension follows its host's session and lifecycle rather than attempting to own them.

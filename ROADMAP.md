# Roadmap

## M0 — Foundation and feasibility

- [x] Establish the canonical repository structure and portable DSP build.
- [x] Record the public-API routing boundary and candidate matrix.
- [x] Implement buffers, prepared nodes, smoothing, gain, meters, filter, EQ, and limiter.
- [x] Run native tests with a C++20 toolchain and resolve all diagnostics.
- [ ] Create the iOS 27 Xcode workspace on a current Apple toolchain.

## M1 — End-to-end hosted alpha

- [x] Add bass enhancer, exciter, stereo tools, and original Crystalizer.
- [x] Upgrade the limiter to 4× true-peak-aware output protection.
- [x] Add latency-compensated click-free global bypass.
- [x] Build an `AVAudioEngine` host for generated test signals.
- [x] Add bounded, predecoded local-file playback with route-safe position retention.
- [x] Add route/session handling, meters, and the first SwiftUI control surface.
- [x] Add durable user-preset persistence and snapshot saving.
- [x] Add validated preset import/export and user-preset selection UI.
- [x] Add progressive-disclosure Advanced DSP bypass and key tuning controls.
- [x] Add inheritable generic iPhone-speaker and provisional AirPods Pro 3 profiles with automatic route switching.
- [ ] Tune generic iPhone-speaker and AirPods Pro 3 profiles on physical hardware.

## M2 — AUv3

- [x] Wrap the same DSP graph in a headless AUv3 effect extension.
- [x] Publish a stable parameter tree and the seven shared factory presets.
- [ ] Validate in representative iOS AUv3 hosts.

## M3 — Experimental routing

- [ ] Implement the highest-value isolated feasibility probe identified in the routing research.
- [ ] Measure latency, stability, route behavior, and content restrictions.
- [ ] Record a proven status; do not promote experimental behavior into production by implication.

## M4 — Serious alpha quality

- [ ] Complete objective response, impulse, distortion, true-peak, and mono-compatibility coverage (initial response, harmonic, and mono regressions landed).
- [ ] Loudness-matched listening tests across genres and listening levels.
- [ ] CPU, thermal, underrun, interruption, and long-duration background tests on iPhone.
- [ ] Accessibility and Dynamic Type audit.

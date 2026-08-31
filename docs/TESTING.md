# Testing

## Automated native tests

The portable test executable covers:

- decibel conversion and parameter smoothing convergence;
- gain processing and finite-sample sanitization;
- all filter shapes producing finite, bounded impulse responses;
- EQ enable/bypass behavior;
- psychoacoustic and extension bass behavior, protection, and bypass;
- high-band exciter contribution and finite output;
- transient-dependent Crystalizer response and bypass;
- stereo width, mono behavior, and correlation telemetry;
- limiter sample ceiling, channel linking, total detector/lookahead latency, gain reduction, and reset;
- quarter-rate intersample-over vectors at 44.1, 48, and 96 kHz whose PCM samples remain below the configured ceiling;
- independent limiter bypass without residual clamping;
- complete seven-stage graph stability and final ceiling;
- latency-matched, crossfaded global bypass;
- sealed PCM-source append, sanitization, end padding, reset, and loop behavior;
- graph preparation bounds, C ABI coverage, and meter publication.

CI is configured to build and run these tests on Linux and macOS with CMake. A separate Ubuntu job runs both native suites under AddressSanitizer and UndefinedBehaviorSanitizer. Spectral golden tests will be added when the required fixtures are in place.

## Automated objective tests

`PhoenauxDSPObjectiveTests` currently enforces:

- at least 23 dB attenuation at 50 Hz from a 200 Hz Butterworth high-pass;
- approximately unity gain at 2 kHz from that filter;
- the expected linear ratio at the center of a +6 dB bell;
- a measurable but bounded 18 kHz third harmonic from a 6 kHz exciter stimulus;
- mono-downmix preservation when neutral mid/balance settings are widened to 1.5×.

These are deterministic regression thresholds, not hardware calibration or a substitute for full swept-sine, standardized true-peak, and THD+N measurement.

## Profile and preset model tests

The Swift model checks cover:

- device-profile inheritance and route-specific bass strategies;
- distinct protective compilation for iPhone speakers and AirPods Pro 3;
- nonlinear intensity curves and device width caps;
- advanced bypass/parameter materialization, safe clamping, and retained device caps;
- complete preset JSON round trips and malformed graph rejection;
- atomic preset save, reload, export/import round trip, and targeted deletion.

On Linux with Swift 6 installed, compile and run the dependency-free model check:

```sh
swiftc -strict-concurrency=complete -warnings-as-errors \
  App/Phoenaux/Models/AdvancedDSPConfiguration.swift \
  App/Phoenaux/Models/DeviceProfile.swift \
  App/Phoenaux/Models/PhoenauxPreset.swift \
  App/Phoenaux/Models/PresetDocument.swift \
  App/Phoenaux/Models/CompiledDSPState.swift \
  App/Phoenaux/Models/PresetStore.swift \
  Tests/LinuxModelCheck/RouteStub.swift \
  Tests/LinuxModelCheck/ModelCheck.swift \
  -o build/PhoenauxModelCheck
./build/PhoenauxModelCheck
```

The generated Xcode project includes equivalent `PhoenauxTests` XCTest coverage. The macOS CI job downloads pinned XcodeGen 2.46.0 and runs `xcodebuild build-for-testing` against a generic iOS Simulator destination. It also compiles a Release build for generic iOS hardware without code signing and uploads `Phoenaux-unsigned.ipa`. The artifact proves device-target compilation and packaging, but it is not installable until signed with an appropriate Apple certificate and provisioning profile; neither this check nor the simulator build replaces signed-device launch or acoustic testing.

Linux CI also type-checks `Tests/LinuxBridgeCheck/BridgeCheck.swift` through the app bridging header. This catches Swift-imported C ABI pointer and integer-shape regressions even though Apple framework sources still require the macOS job.

The same build includes and embeds `PhoenauxAUv3`, so Apple CI compilation covers its factory, bus negotiation, parameter tree, and render-wrapper source. Runtime discovery, state restoration, automation, mono/stereo negotiation, and out-of-process render behavior still require representative AUv3 hosts.

## Additional objective DSP coverage planned

- swept-sine magnitude/phase response against analytical targets;
- expanded impulse and step response;
- complete THD+N and aliasing sweeps for nonlinear modules;
- expanded ITU-R/EBU true-peak conformance vectors across sample rates, frequencies, and phases;
- automation stress, NaN/Inf, denormal, and extreme-parameter fuzzing;
- expanded mono compatibility and stereo correlation vectors;
- latency and bypass-alignment tests.

## Apple integration matrix planned

- iOS 27 device hosted playback at 44.1/48 kHz with generated, mono-file, and stereo-file sources;
- built-in speaker, AirPods Pro 3, generic Bluetooth, wired/USB, AirPlay;
- calls, Siri, alarms, lock, route loss, Bluetooth reconnect, media-services reset;
- foreground/background and long-duration playback;
- AUv3 hosts with parameter automation and state restoration.

## Listening protocol

Preset judgments use level-matched bypass comparisons, multiple genres, low and moderate listening levels, and fatigue checks. “Better” is not accepted when it is explained only by greater loudness.

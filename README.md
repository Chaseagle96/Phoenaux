# Phoenaux

## Audio, Reborn.

Phoenaux is an iOS audio-enhancement project built around a reusable, real-time-safe DSP engine. Its goal is a lively, powerful, device-aware listening profile without pretending that stock iOS exposes a global output equalizer.

The planned flagship chain is:

`Filter -> Equalizer -> Bass Enhancer -> Exciter -> Crystalizer -> Stereo Tools -> Limiter`

## Current state

This repository is at the foundation stage. It currently contains:

- a portable C++20 `PhoenauxDSP` library;
- prepared, in-place non-interleaved float processing;
- control-to-render parameter smoothing and lock-free metering;
- all seven flagship stages: Filter, parametric EQ, Bass Enhancer, Exciter, Crystalizer, Stereo Tools, and a 4× true-peak-aware lookahead limiter;
- latency-matched, crossfaded global bypass plus independent stage bypass;
- versioned, inheritable device profiles with conservative iPhone-speaker and provisional AirPods Pro 3 tuning;
- validated, shareable JSON preset documents with atomic persistence, import/export, and saved-preset selection;
- progressive-disclosure Advanced DSP controls with live stage bypass and device-capped authored tuning;
- nonlinear compilation of output profile, user preset, and Reborn intensity into immutable DSP state;
- hosted generated-signal and local mono/stereo file playback through the shared DSP graph;
- interruption-aware transport plus route/sample-rate and media-services reset recovery;
- a headless AUv3 effect target with all seven stages, independent bypass controls, stable parameters, factory presets, and user-preset state support;
- deterministic native behavior and objective response tests with cross-platform CI configuration;
- macOS-runner packaging of an unsigned device-build IPA artifact for later Apple signing;
- an evidence-backed routing analysis for current Apple platforms.

The limiter has automated intersample-over protection coverage but is not yet certified against a complete ITU-R/EBU true-peak vector suite. The iOS host, SwiftUI source, local-file transport, and AUv3 target are present but have not been built locally on an Apple toolchain. GitHub CI compiles the generated iOS project, embedded extension, bridge checks, and model tests, then packages an unsigned device-build IPA artifact. Measured device calibration, AUv3 host validation, and physical-hardware validation remain in progress.

## Build the portable DSP tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

An Apple development machine with the current Xcode release is required to create, sign, run, and measure the iOS 27 application and AUv3 extension. The portable core deliberately does not depend on SwiftUI or a routing API.

The iOS project is described by `project.yml` so its generated Xcode project does not accumulate machine-specific project-file noise. On a Mac with [XcodeGen](https://github.com/yonaskolb/XcodeGen) installed:

```sh
xcodegen generate
open Phoenaux.xcodeproj
```

The app target can host either its generated stereo test signal or a user-selected local mono/stereo audio file through the real shared DSP chain. Local files are decoded off the audio thread into a bounded, sealed PCM source before playback; protected or unsupported media remains outside the app's routing promise.

## Routing promise

The production app will process audio that Phoenaux owns or legally receives. The AUv3 extension will process audio supplied by a compatible host. The project does not claim to process arbitrary third-party app audio system-wide on stock iPhone; see [Audio Routing Research](docs/AUDIO_ROUTING_RESEARCH.md).

## License status

No project license has been selected yet. All initial DSP code in this repository is an original clean-room implementation. Do not redistribute it as open source until a license is added.

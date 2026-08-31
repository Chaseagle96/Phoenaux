# ADR 0004: Headless AUv3 Wrapper

- Status: Accepted
- Date: 2026-08-31

## Decision

Ship Phoenaux initially as a headless `aufx` Audio Unit extension. The extension subclasses `AUAudioUnit`, exposes stable host parameters and seven factory presets, and renders by pulling host input into the output buffers before calling the existing prepared `PhoenauxDSP` C ABI in place.

## Rationale

A headless extension proves host interoperability without duplicating the app UI or DSP. It follows Apple’s extension factory model, keeps the render callback bounded, and lets compatible hosts provide their own generic parameter interface. The shared C++ graph ensures app and AUv3 sound changes do not diverge.

## Consequences

The first bus contract is matching mono or stereo, noninterleaved Float32 PCM. Device profiles remain app-owned because an AUv3 host does not provide a reliable output-device identity contract. Factory values use conservative generic tuning. Parameter changes are atomic and block-rate; sample-accurate event slicing and a custom extension UI remain future work. The extension must be compiled and exercised in real hosts before it is considered proven.

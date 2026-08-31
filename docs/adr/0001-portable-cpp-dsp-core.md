# ADR 0001: Portable C++ DSP Core

- Status: Accepted
- Date: 2026-08-30

## Decision

Implement sample processing in a C++20 static library with an opaque C ABI. Swift, hosted `AVAudioEngine`, AUv3, offline tests, and isolated experiments use the same engine.

## Rationale

C++ provides deterministic ownership, portable native tests, direct control of render-thread allocation, and no dependency on SwiftUI or one routing architecture. The C ABI prevents Swift and Audio Unit targets from depending on C++ standard-library layout.

## Consequences

Apple adapters must translate `AudioBufferList` layouts safely. Build tooling must compile C++20. Accelerate-specific kernels may be added behind the portable API, but the canonical algorithm remains independently testable.

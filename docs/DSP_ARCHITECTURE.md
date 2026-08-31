# DSP Architecture

## Render format

The first engine contract is 32-bit floating-point, non-interleaved PCM. `AudioBufferView` borrows channel pointers and frame counts; it owns no sample memory. Apple adapters are responsible for validating layouts and converting only when needed.

`PhoenauxDSP.h` provides an opaque C ABI for Swift and Audio Unit adapters. Allocation and destruction are explicit control-thread operations. Rendering through `PXEngineProcess` performs no ownership transfer and calls the same `DSPChain` exercised by native tests.

`ProcessSpec` establishes sample rate, maximum block size, and maximum channel count before rendering. A node must reject unsupported preparation rather than discover it in the callback.

The portable `PXPCMSource` is a transport-side companion, not a DSP stage. Control code appends decoded non-interleaved samples and seals the source before playback. Its render function then performs only bounded copies, loop/end decisions, silence padding, and atomic position publication; allocation, mutation, decoding, and file access are prohibited after sealing.

The AUv3 wrapper prepares from the host-negotiated bus format and `maximumFramesToRender`. Its render block performs only the host input pull, frame/bus bounds checks, and one `PXEngineProcessAudioBufferList` call. Parameter-tree observers update the same atomic control targets used by the hosted app; no SwiftUI or persistence model enters rendering.

## State flow

```text
editable UI/preset values
  -> validation and profile resolution
  -> atomic parameter targets
  -> render-owned smoothers and filter state
  -> samples + atomic meter publication
```

Atomics are for small control values and telemetry, not graph ownership. The processing side owns every smoother, history sample, detector envelope, and delay line.

The current fixed flagship graph is `Input Gain -> Filter -> EQ -> Bass Enhancer -> Exciter -> Crystalizer -> Stereo Tools -> Limiter -> Output Gain`. Each module processes continuously through a smoothed wet/bypass transition so state remains warm. Global bypass uses a preallocated dry delay matched to limiter lookahead, then crossfades at the graph output.

## Graph evolution

The initial core exposes prepared nodes and an ordered graph. The full graph compiler will:

1. validate order and parameters off-thread;
2. instantiate and prepare all nodes and scratch storage off-thread;
3. publish a ready state at a render boundary;
4. retire the old state on a non-real-time thread.

This avoids allocation and destruction in rendering. Bypass transitions will use bounded crossfades; latency-producing nodes require a matched dry delay.

## Safety invariants

- Frame and channel counts never exceed the prepared maxima.
- Public control setters clamp or reject unsafe values.
- Non-finite input and state are sanitized.
- Biquad parameters are clamped below Nyquist and to a positive Q.
- Per-node reset clears histories without allocation.
- Limiter and global-bypass delay lines are allocated only during preparation.

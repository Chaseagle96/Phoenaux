# Architecture

Phoenaux separates signal processing from transport, product state, and presentation so the same algorithms can serve hosted playback, AUv3, offline tests, and isolated experiments.

```text
SwiftUI and feature models
          |
validated presets + device profile resolution
          |
compiled control state
          v
  Apple transport adapters  ----->  route/session observer
  (hosted player or AUv3)
          |
          v
     PhoenauxDSP (C++20)
  buffer -> ordered nodes -> meters
```

## Boundaries

- `PhoenauxDSP` owns sample processing only. It has no SwiftUI, filesystem, network, logging, or Apple audio-session dependency.
- Apple adapters translate `AudioBufferList` data into bounded `AudioBufferView` values and call prepared DSP objects. The hosted engine and headless AUv3 effect both use the same C ABI and C++ graph.
- Local media decoding and security-scoped URL access occur off the render thread. The decoder fills and seals a portable PCM source before an `AVAudioSourceNode` can read it; playback never reads a file, allocates sample storage, or touches a URL.
- Editable presets and device profiles remain control-thread models. They are validated and compiled before render activation.
- Experimental routing code belongs under `Experimental/` and must never be linked silently into the App Store target.

## Render-thread contract

Every DSP node has a preparation phase and a `noexcept` processing phase. Preparation may allocate and calculate configuration. Processing must not allocate, lock, perform I/O, log, access UI state, or run an unbounded loop. Parameter values cross from control code through atomics and are smoothed by render-owned state.

The first graph is an ordered in-place chain. Reordering will use an off-thread graph compiler that produces a fully prepared state before activation. The eventual swap mechanism must ensure retirement and destruction occur away from the render thread; an atomic `shared_ptr` alone is not sufficient because the final release can deallocate on the caller.

## Source layout

- `Sources/PhoenauxDSP/`: portable production DSP.
- `Tests/PhoenauxDSPTests/`: deterministic native tests.
- `docs/`: routing, algorithm, format, and validation decisions.
- `App/`: hosted playback, route/session handling, product state, and SwiftUI.
- `Extensions/PhoenauxAUv3/`: host-loaded effect wrapper, stable parameter tree, and factory presets.
- `Experimental/`: isolated routing probes only.
- `docs/adr/`: lightweight records for consequential architecture decisions.

Architecture decisions that change these boundaries must update this file and relevant tests in the same change.

# Performance

## Render budget

The core must complete comfortably inside the hardware I/O period at 44.1 and 48 kHz, with higher rates treated as measured capabilities rather than assumptions. Per-block measurements will record median, p95, p99, and worst render duration along with underruns.

Algorithmic latency is reported separately from AVAudioEngine buffering, hardware, and Bluetooth transport latency. The true-peak-aware limiter contributes its configured lookahead plus eight input frames for its causal interpolation detector. At 48 kHz, the default 5 ms setting therefore reports 248 frames, or approximately 5.17 ms. The global bypass delay uses this same value. Every future latency-producing node must expose its frame latency to the graph.

The limiter detector performs four 16-tap interpolation phases per input channel and frame. This bounded cost avoids allocating a 4× audio stream in rendering; actual CPU impact still requires profiling on supported iPhones and in out-of-process AUv3 hosts.

Hosted local-file playback trades streaming I/O complexity for a sealed predecoded source. Decoded float PCM is capped at 256 MiB and supports one or two channels. This keeps URL access, codec work, and sample-storage growth outside rendering; device memory behavior still requires measurement before increasing the cap or adding multichannel support.

## Rules

- No allocation, deallocation, locks, I/O, logging, or UI access in `process`.
- Scratch and delay storage are sized during `prepare`.
- Loops are bounded by prepared channel and frame maxima.
- Optimizations require benchmark and response-equivalence evidence.
- Accelerate/vDSP may be used in Apple adapters or specialized kernels without making the portable model dependent on SwiftUI.

## Required device measurements

- CPU and render duration per module and full chain;
- memory footprint and allocation tracing during playback;
- thermal and battery behavior over long sessions;
- underruns during UI interaction, route changes, and background playback;
- AUv3 out-of-process overhead at representative buffer sizes;
- limiter and complete-chain latency by impulse measurement.

No physical-iPhone performance claim has been validated yet.

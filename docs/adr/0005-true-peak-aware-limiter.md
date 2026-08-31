# ADR 0005: True-Peak-Aware Limiter

- Status: Accepted
- Date: 2026-08-31

## Decision

Replace sample-only detection with a linked 4× polyphase detector built from four normalized 16-tap Hann-windowed sinc phases. Add the detector’s eight-frame causal latency to the configured lookahead and hold attenuation until each detected event reaches the delayed output.

## Rationale

Enhanced audio can remain below a digital sample ceiling while its reconstructed waveform exceeds that ceiling between samples. Detector-only oversampling finds those events without allocating or processing a complete 4× audio stream. Fixed-size coefficients and history keep render work bounded, while explicit detector latency preserves global-bypass alignment.

## Consequences

At 48 kHz, detector latency adds about 0.17 ms. The detector costs 64 multiply-accumulates per channel and input frame. The implementation has independent intersample-over regressions at 44.1, 48, and 96 kHz but is not yet certified against a complete standardized true-peak vector set, so documentation must say “true-peak-aware” rather than imply formal compliance.

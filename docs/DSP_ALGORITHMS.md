# DSP Algorithms

## Gain

Gain is represented in decibels at the control boundary and converted to linear amplitude on the render side. Changes use a bounded linear ramp to prevent zipper noise. The implementation sanitizes non-finite input samples.

## Biquad filter and EQ

The first Filter and EQ stages use normalized second-order biquads based on the public-domain RBJ Audio EQ Cookbook equations. Supported shapes are high-pass, low-pass, low shelf, high shelf, bell, band-pass, and notch. Parameters are clamped to defensible ranges. During automation, frequency, Q, and gain are smoothed and coefficients are recalculated from the smoothed values.

Recalculating coefficients per moving sample favors continuity and correctness in this initial build. A later optimization may use a proven pole/zero interpolation or control-rate strategy after response and stability tests demonstrate equivalence.

The parametric EQ contains a fixed maximum number of prepared bands. Disabled bands retain state reset semantics and consume no sample processing. Automatic headroom estimation is not implemented yet; presets must carry conservative preamp values.

## True-peak-aware limiter

The limiter is linked across channels and uses a prepared 4× polyphase detector. Each phase is a normalized 16-tap Hann-windowed sinc interpolator. The detector estimates peaks between stored PCM samples without creating an oversampled audio buffer. Its history, coefficients, audio delay, and bypass delay are allocated or calculated only during `prepare`.

The audio path is delayed by the configured lookahead plus eight input frames of detector latency. Gain attack is immediate and stereo-linked. A hold counter prevents release until a detected event reaches the delayed output; exponential release follows. A final sample clamp remains as a numerical guard. Independent bypass retains the full declared delay but applies neither gain reduction nor clamping.

Automated quarter-rate, quarter-sample-phase sine vectors at 44.1, 48, and 96 kHz prove that the detector reduces a waveform whose stored samples remain below the ceiling while its reconstructed peak exceeds it. This is true-peak-aware protection, not a claim of complete ITU-R BS.1770 or EBU conformance. More frequencies, phases, and standardized vectors remain required before certification language is appropriate. The default ceiling remains approximately -1 dBTP.

Final output trim follows the limiter in the published graph but is constrained to attenuation or unity. Positive gain must be introduced before limiting, so no product control can amplify a protected output after the safety boundary.

## Bass Enhancer

The Bass Enhancer has two original clean-room modes sharing a protected low-band detector. `psychoacoustic` extracts the low band, applies bounded nonlinear shaping, removes the shaped fundamental region, and mixes the resulting audible harmonics in parallel. This is intended for small speakers that cannot reproduce deep fundamentals. `extension` reinforces the extracted fundamental more conservatively for capable headphones. A release envelope reduces enhancement as low-band amplitude rises.

The current implementation is a first nonlinear prototype. Oversampling, route-specific calibration, and harmonic-spectrum regression fixtures remain required before production tuning.

## Exciter

The Exciter subtracts a smoothed low-pass signal to obtain a high band, applies bounded nonlinear shaping, removes low-frequency content from the generated residual, clamps the parallel contribution, and mixes it with the dry signal. Frequency, drive, amount, mix, and bypass are bounded and smoothed.

The objective suite measures the generated third harmonic for an exact-bin 6 kHz stimulus and constrains its ratio to the retained fundamental. This guards both accidental linearization and runaway contribution; broader THD+N and aliasing sweeps remain required.

## Crystalizer

Crystalizer is not a static treble shelf. It extracts a configurable detail band and compares fast and slow amplitude envelopes. Only positive short-term temporal contrast opens the enhancement path, which adds a bounded portion of the detail signal in parallel. Sensitivity controls the transient gate; amount and mix control contribution. This is an original Phoenaux algorithm subject to future multiband and aliasing refinement.

## Stereo Tools

Stereo Tools converts the first stereo pair to mid/side, applies smoothed mid gain, side gain, and width, then reconstructs left/right. It also provides balance, channel swap, mono, per-channel polarity inversion, and a one-pole mono-bass blend. Block correlation is published atomically. Production presets constrain width even though the advanced parameter range permits stronger settings.

With neutral mid gain and balance, width changes are required to preserve the arithmetic mono downmix. A deterministic 1.5×-width regression enforces this invariant with independent left/right tones.

## Bypass

Every creative processor crossfades its wet contribution. The limiter retains its declared delay while independently bypassed but no longer applies gain reduction or ceiling clamping. Global bypass runs the graph to keep state warm and crossfades to a preallocated dry path delayed by the limiter latency, preserving synchronization during rapid A/B comparisons.

## Further work

- standardized true-peak conformance vectors and adaptive limiter release;
- oversampled nonlinear kernels and measured aliasing limits;
- higher-order Linkwitz-Riley mono-bass crossover;
- multiband Crystalizer with energy-normalized transient extraction;
- objective parameter-automation and response fixtures.

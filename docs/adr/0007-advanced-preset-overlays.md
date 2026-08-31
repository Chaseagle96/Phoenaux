# ADR 0007: Advanced Edits as Preset Overlays

- Status: Accepted
- Date: 2026-08-31

## Decision

Represent live Advanced DSP edits as sparse control-thread overrides on the selected preset. Materialize those overrides into a complete `PhoenauxPresetDocument` before DSP compilation, snapshot saving, or export. Clear them when a different preset becomes active.

## Rationale

Maintaining a second advanced-only render model would allow playback, saved presets, and shared presets to disagree. Editing the immutable built-in catalog directly would also blur factory authorship and user state. A sparse overlay supports reversible live experimentation while reusing existing validation, serialization, nonlinear intensity, and device-profile compilation.

## Consequences

Advanced values are authored intent, not permission to bypass device protection. Profile high-pass, bass strategy, and stereo-width caps still apply during compilation. Disabling the limiter is permitted for explicit independent-stage bypass testing, but the UI warns that final output protection is then absent. Saving or exporting captures the effective advanced state in the standard schema without introducing a parallel format.

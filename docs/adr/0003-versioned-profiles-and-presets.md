# ADR 0003: Versioned Profiles and Presets

- Status: Accepted
- Date: 2026-08-30

## Decision

Represent device profiles as versioned inheritable definitions, presets as validated versioned JSON documents, and rendering controls as an immutable compiled state. Compile:

`route profile + preset + intensity -> CompiledDSPState`

before updating DSP atomics.

## Rationale

Profiles and creative presets solve different problems. Inheritance allows conservative family fallbacks and narrow model overrides without copying entire graphs. A compiled state prevents persistence/UI models from entering the render callback. Separate tonal, bass, detail, and width interpolation curves avoid simplistic linear Intensity scaling.

## Consequences

Unknown schemas and modules fail validation. Graph order is serialized now, but this build rejects noncanonical order until the off-thread graph compiler exists. AirPods Pro 3 matching and tuning remain explicitly provisional pending stable route evidence and physical measurements.

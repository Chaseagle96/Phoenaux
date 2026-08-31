# Preset Format

The serialized preset format is UTF-8 JSON with an explicit schema version. JSON is chosen for inspectability and shareability; runtime code consumes a validated `CompiledDSPState` instead of parsing in the render callback. `PresetStore` writes atomically under Application Support with complete file protection and validates imported documents before persistence.

```json
{
  "schemaVersion": 1,
  "identifier": "com.phoenaux.preset.reborn",
  "name": "Reborn",
  "profileCompatibility": ["generic"],
  "graphOrder": [
    "filter", "equalizer", "bassEnhancer", "exciter",
    "crystalizer", "stereoTools", "limiter"
  ],
  "inputGainDB": -4.0,
  "outputGainDB": 0.0,
  "authoredIntensity": 1.0,
  "modules": [
    { "kind": "filter", "enabled": true, "parameters": {} },
    { "kind": "equalizer", "enabled": true, "parameters": {} },
    { "kind": "bassEnhancer", "enabled": true, "parameters": {} },
    { "kind": "exciter", "enabled": true, "parameters": {} },
    { "kind": "crystalizer", "enabled": true, "parameters": {} },
    { "kind": "stereoTools", "enabled": true, "parameters": {} },
    { "kind": "limiter", "enabled": true, "parameters": {} }
  ]
}
```

## Rules

- Unknown top-level fields are ignored by the current Swift decoder; forward-compatible retention remains future work.
- Unknown module kinds make a preset unsupported rather than being silently discarded.
- Values are finite, range-checked, and normalized during compilation.
- Built-in presets have immutable identifiers and revisioned content.
- User presets receive new identifiers and retain their original profile provenance.
- Graph order and bypass state are part of the preset.
- Intensity is a structured interpolation between authored states, not a scalar applied indiscriminately to every parameter.
- Noncanonical graph order is preserved but rejected by this fixed-graph build rather than being silently ignored.

The built-in catalog contains Reborn, Pure, Impact, Crystal, Wide, Voice, and Night with distinct authored intent. Their values are provisional until loudness-matched device listening tests are complete. Users can save and select validated snapshots, import JSON through a security-scoped document URL, and export the active built-in or saved preset through SwiftUI's document exporter. An imported noncanonical graph is preserved but is not selected or described as applied by this fixed-graph build.

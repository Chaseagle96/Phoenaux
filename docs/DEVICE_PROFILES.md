# Device Profiles

Device profiles are versioned tuning inputs, not claims of laboratory calibration. A profile combines route matching, supported formats, graph order, conservative gain staging, preset overlays, and provenance notes.

The app now implements `DeviceProfileDefinition`, `ResolvedDeviceProfile`, and `DeviceProfileCatalog`. Resolution walks a parent chain with cycle and missing-parent detection, then produces a complete immutable profile. The initial chain includes Conservative Generic, Generic iPhone Speaker, AirPods Family, AirPods Pro Family, and an explicitly provisional AirPods Pro 3 leaf.

## Resolution order

```text
generic output family
  -> product family when reliably observed
  -> exact model only when stable evidence exists
  -> user preset overlay
  -> intensity interpolation
  -> validated render configuration
```

Unknown routes always fall back to a conservative generic profile. Exact model matching must never depend solely on a localized display name.

Route changes resolve a new profile and compile a new DSP state. If playback is active, Phoenaux rebuilds the audio engine so a hardware sample-rate change cannot leave stale filter coefficients active.

## Generic iPhone speaker

Initial objectives are speech clarity, controlled upper-bass harmonics, transient definition, modest compatible width, and strict peak protection. The profile must avoid wasting headroom on physically unreproducible sub-bass. Device-specific EQ awaits repeatable measurements.

## AirPods Pro 3

Initial objectives are controlled extension and punch, restrained presence/detail enhancement, and mono-compatible width. Tuning must account for Apple's built-in and mode-dependent processing through real-device listening and measurements. Until stable route identification and measurements exist, Phoenaux uses a conservative AirPods Pro-family fallback and labels it accordingly.

## Versioning

Profiles carry schema version, profile revision, parent identifier, supported route predicates, sample-rate constraints, parameters, gains, and tuning provenance. Updating a shipped profile creates a new revision and migration decision; it does not silently reinterpret user-authored presets.

The current numerical values are protective engineering defaults, not measured correction curves. The AirPods Pro 3 route-name match is best-effort and falls back to the Pro family when exact identity is unavailable.

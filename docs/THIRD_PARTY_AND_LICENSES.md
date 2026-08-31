# Third-Party Code and Licenses

The initial repository has no vendored third-party source or binary dependency.

XcodeGen 2.46.0 is an optional MIT-licensed development tool used to materialize `Phoenaux.xcodeproj` from the checked-in `project.yml`. CI downloads that pinned release from the upstream GitHub project. It is not linked or shipped in the app.

The biquad designs follow the equations published in Robert Bristow-Johnson's Audio EQ Cookbook. Those equations are a documented technical reference; the implementation here was written independently for Phoenaux.

EasyEffects, ViPER4Android, Equalizer APO, AutoEQ, commercial audio units, and mastering tools may be studied for public behavior, workflows, and DSP concepts. Their source, branded assets, preset data, measurements, and proprietary algorithms must not be copied unless a compatible license is identified, recorded here, and its obligations are fulfilled.

Before adding any dependency or imported tuning dataset, record:

- project and source URL;
- exact version or commit;
- license and copyright notice;
- files or targets that use it;
- redistribution, attribution, source-disclosure, and patent obligations;
- whether it is acceptable for the main app, AUv3, and experimental targets.

Phoenaux itself does not yet have a selected distribution license.

# ADR 0006: Sealed Local PCM Source

- Status: Accepted
- Date: 2026-08-31

## Decision

Decode user-selected local mono/stereo files off the render thread into a portable, non-interleaved float PCM source capped at 256 MiB. Append operations are allowed only before an explicit seal; playback reads immutable channel vectors through a bounded C ABI render call.

## Rationale

Direct `AVAudioFile` reads in an audio callback would introduce file I/O, codec work, Objective-C calls, and uncertain timing. A sealed source gives the hosted player deterministic callback work and lets native tests cover end-of-file, silence padding, position, reset, and looping independently of Apple frameworks. The memory cap prevents an unbounded decode from exhausting the process.

## Consequences

Playback starts only after the complete file is decoded, and long files above the cap are rejected rather than streamed. Route-driven engine rebuilds can retain a source's atomic frame position. Gapless playlists, seeking, very long media, protected content, and multichannel files require a future bounded producer/consumer transport with the same render-thread rules.

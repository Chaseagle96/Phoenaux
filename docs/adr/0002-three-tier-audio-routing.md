# ADR 0002: Three-Tier Audio Routing

- Status: Accepted
- Date: 2026-08-30

## Decision

Maintain separate production hosted playback, AUv3, and experimental routing targets. Do not describe hosted or host-selected AUv3 processing as system-wide.

## Rationale

Public iPhone APIs expose app-owned audio graphs and host-loaded effects, not a documented insertion point into arbitrary applications' final output. Isolating experimental mechanisms protects App Store-safe targets while preserving the long-term routing objective.

## Consequences

Routing remains an adapter around `PhoenauxDSP`. Experimental code requires explicit target membership and documentation. Capability claims must cite evidence in `AUDIO_ROUTING_RESEARCH.md`.

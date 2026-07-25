# DM-2026-001 — Build System Selection

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (review issue #2, OQ-01: "cmake is fine to install in this environment")
- **Drafted by:** AI agent (Claude)
- **Resolves:** OQ-01
- **Affects:** REQ-9.6 (amended)

## Decision

**CMake** is the build system. It is a build-time tool only; the stdlib-first runtime
dependency posture (REQ-9.4) is unaffected.

## Options Considered

### Option 1 — CMake (selected)

- Conventional for C++; every IDE and CI system understands it.
- Clean path to future targets contemplated in the spec: other platforms (REQ-9.5),
  a WASM/browser build for the future presentation layer (REQ-1.5).
- Cost: one `brew install cmake` on a fresh machine (installed 2026-07-25:
  cmake via Homebrew, arm64).

### Option 2 — Hand-written Makefile

- Zero install; satisfied the original REQ-9.6 wording literally.
- Rejected: scales poorly as targets multiply (core lib, CLI, tests, fixtures), and
  Apple's make is ancient GNU make 3.81. The zero-install property purchased complexity
  everywhere else.

## Consequence

REQ-9.6 amended to: build succeeds from clean checkout with Xcode Command Line Tools
**and CMake**; nothing else required.

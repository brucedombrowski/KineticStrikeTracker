# Changelog

All notable changes to this project are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- REQ-2026-001 v0.3.0 (issue #20, DM-2026-005): RFC 9562 replaces obsoleted RFC 4122
  (pinned to name-based UUIDv5 — time-ordered variants excluded for determinism),
  ISO/IEC 25010:2023 and NIST SP 800-53 Rev. 5 pinned, RFC 8446 added as TLS baseline
  (1.2 min / 1.3 preferred), REQ-13.6 SBOM-at-release added. OpenAPI and C++23 rejected
  for v1 with recorded rationale. 104 requirements, 17 standards.

### Changed
- REQ-2026-001 bumped to v0.2.0 applying the human review received as issue #2 — all seven
  open questions resolved. REQ-1.1 (whole-Earth model), REQ-2.4 (ISC as the seismic
  aggregation route; direct IRSC excluded by its no-redistribution licence), and REQ-9.6
  (CMake approved as build-time tool) amended; REQ-1.6 (coverage model), REQ-2.18 (curated
  seed dataset), REQ-7.7 (representation-agnostic confidence), REQ-8.8 (coverage grey-out
  layer) added — 103 requirements total.

### Added
- Build skeleton (issue #5): CMake project, `kst_core` static library separated from the
  thin `kst` CLI (REQ-9.2), stdlib-only test harness registered with CTest (REQ-10.2),
  warnings-as-errors set documented in the root CMakeLists (REQ-9.7). CLI verbs from
  REQ-8.1 present as honest stubs (exit 2, not fake success).
- Phase 2 backlog seeded as issues #5–#19, each traced to its requirements.
- `requirements/evidence/isc-iran-jun2025.txt` — ISC FDSN query result (OQ-07 spike):
  thirteen IDC (CTBTO) depth-0.0 origins clustered on June 2025 strike nights, basis for
  ASM-08 and validation case VC-04.
- Decision memoranda DM-2026-001 (CMake), DM-2026-003 (confidence representation),
  DM-2026-004 (triangulation deferred), all decided by human review issue #2.
- Repository scaffolding per the
  [systems-engineering](https://github.com/brucedombrowski/systems-engineering) process framework.
- `requirements/REQ-2026-001.json` — draft requirements specification, 99 requirements across 13
  categories, RFC 2119 keywords, IEEE 29148 identifiers, per-requirement standard citation and
  verification method.
- `requirements/evidence/` — USGS FDSN query snapshots for Iran, June 2025, retained as the
  measured basis for the detection-floor assumption (ASM-01).
- Validation cases VC-01 through VC-03 derived from the June 2025 Iran campaign.
- `.gitignore` derived from the shared `templates/gitignore-base`, extended for C++ and macOS
  build output and for API credential patterns.
- `LICENSE` — Apache-2.0, selected per `docs/decisions/DM-2026-002-license-selection.md`
  (resolves OQ-02, satisfies REQ-13.1).

### Notes
- Requirements are **draft, pending human review**. They become the v1.0 baseline only on an
  approving review, per process principle 1 (review over authoring).

[Unreleased]: https://github.com/brucedombrowski/KineticStrikeTracker/commits/main

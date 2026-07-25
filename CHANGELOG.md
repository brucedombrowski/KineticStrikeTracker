# Changelog

All notable changes to this project are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
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

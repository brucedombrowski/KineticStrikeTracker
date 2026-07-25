# DM-2026-005 — External Standards Review Dispositions

- **Date:** 2026-07-25
- **Status:** Approved
- **Input:** Issue #20 — governing-standards review produced by Google Gemini, submitted by
  Bruce Dombrowski
- **Adjudicated by:** AI agent (Claude), dispositions recorded here for human visibility
- **Affects:** REQ-2026-001 v0.3.0

## Context

A third-party AI review of the `governing_standards` array. Treated like any external
input: each recommendation adjudicated on merit against project requirements, not adopted
wholesale.

## Dispositions

| Recommendation | Disposition | Rationale |
|---|---|---|
| RFC 2119 → BCP 14 | **Already applied** | Issue #17, commit `7c447a6`, upstream backport systems-engineering#7 |
| RFC 4122 → RFC 9562 | **Accepted, with correction** | RFC 9562 obsoleted RFC 4122 (May 2024) — citation updated. But the review's motivation (time-sorted UUIDv7 for index speed) is **inapplicable and harmful here**: time-ordered or random identifiers violate determinism (REQ-1.2, REQ-3.5). We pin **name-based UUIDv5, RFC 9562 §5.5**, which the new RFC retains. |
| ISO/IEC 25010 → :2023 | **Accepted** | Revision pinned; the 2023 overhaul is current. |
| NIST SP 800-53 → Rev. 5 | **Accepted** | Revision pinned; control IDs unchanged. |
| Add RFC 8446 (TLS 1.3) | **Accepted, modified** | Cited as data-in-transit baseline: TLS 1.2 minimum, 1.3 preferred. A client-side mandate of 1.3-only is rejected — upstream seismic services control their own endpoints, and refusing 1.2 would silently drop mandatory sources (contradicting REQ-2.9's coverage-honesty posture). |
| Add SBOM (SPDX 3.0 / CycloneDX 1.5) | **Accepted as SHOULD** | New REQ-13.6. Under stdlib-first the SBOM is small and cheap. SPDX 2.3 retained for now over 3.0: 3.0 is a major restructure with immature tooling; revisit at first release. |
| Add OpenAPI 3.1.0 | **Rejected for v1** | The system consumes REST services; it exposes none. Becomes relevant with the future browser layer (REQ-1.5) — noted there, not levied now. |
| C++20 → C++23 | **Rejected for v1** | Apple Clang C++23 support is incomplete on the target toolchain; C++20 is fully supported and sufficient. (The review's claim of C++23 "networking elements" is incorrect — the standard contains no networking library.) Revisit when the toolchain matures. |

## Consequence

REQ-2026-001 bumped to v0.3.0: 104 requirements, 17 governing standards.

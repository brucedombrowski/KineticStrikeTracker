# Decision Memoranda

Formal records of design decisions, per Phase 3 of the
[systems-engineering](https://github.com/brucedombrowski/systems-engineering) process framework.

Naming: `DM-YYYY-NNN-short-title.md`

Each memorandum records a minimum of two options considered, the selection, and the rationale with
references to governing requirements or standards.

## Decided

| ID | Subject | Outcome |
|---|---|---|
| [DM-2026-001](DM-2026-001-build-system.md) | Build system | CMake (build-time only; REQ-9.6 amended) |
| [DM-2026-002](DM-2026-002-license-selection.md) | Open source licence selection | Apache-2.0 |
| [DM-2026-003](DM-2026-003-confidence-representation.md) | Confidence representation | Store factors, derive presentation (REQ-7.7) |
| [DM-2026-004](DM-2026-004-triangulation-scope.md) | Independent triangulation in v1 | Deferred to backlog |
| [DM-2026-005](DM-2026-005-external-standards-review.md) | External (Gemini) standards review dispositions | 6 accepted/modified, 2 rejected with rationale |
| [DM-2026-006](DM-2026-006-raw-response-retention.md) | Raw-response retention | Retain; append-only store (OQ-08) |
| [DM-2026-007](DM-2026-007-depth-provenance-and-format-choice.md) | Depth provenance, format choice | Read QuakeML; choose formats by what they disclose (OQ-09) |
| [DM-2026-008](DM-2026-008-diurnal-discriminant.md) | Diurnal-regularity discriminant | Working-hours signature, daylight-constrained, one-way (REQ-5.12) |
| [DM-2026-009](DM-2026-009-reports-in-correlation.md) | What a report may contribute to an event | Instrumental-only formation; attach with ambiguity (issue #29) |
| [DM-2026-010](DM-2026-010-coverage-gap-rule.md) | When a window counts as unobserved | Incomplete on internal disagreement, not on emptiness (issue #27) |

DM-2026-001 – 004 decided by human review, issue #2; DM-2026-005 adjudicates issue #20 (2026-07-25).
DM-2026-006 – 008 resolve OQ-08 – OQ-10 (2026-07-25). DM-2026-009 and DM-2026-010 arose from the
2026 Strait of Hormuz validation work and resolve issues #29 and #27 (2026-09-03).

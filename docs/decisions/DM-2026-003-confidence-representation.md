# DM-2026-003 — Confidence Representation

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (review issue #2, OQ-05)
- **Drafted by:** AI agent (Claude)
- **Resolves:** OQ-05
- **Affects:** REQ-7.7 (added)

## Decision

Defer the numeric-vs-ordinal choice **by design rather than by indecision**: the system
stores the individual confidence factors and derives the presentation. Changing or even
running both representations is a view-layer concern that never invalidates stored data
or requires re-analysis (REQ-7.7).

## Options Considered

1. **Numeric score** — sorts and thresholds naturally; invites false precision.
2. **Ordinal bands** — resists false precision; coarse for ranking.
3. **Representation-agnostic storage, presentation derived (selected)** — Bruce's direction:
   "design system to not break if we choose one to start and then decide to go another way later."

## Rationale

The factors (source class weights, independence count, disconfirming evidence, coverage
context) are the auditable substance; any scalar or band is a projection of them. Storing
projections instead of factors would be an irreversible information loss for a system whose
defining property is reproducibility.

# DM-2026-004 — Independent Triangulation: v1 Scope

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (review issue #2, OQ-06: "fine to defer, add it to the backlog")
- **Drafted by:** AI agent (Claude)
- **Resolves:** OQ-06
- **Affects:** REQ-5.8, REQ-5.9 (remain recommended; deferred from v1)

## Decision

Independent triangulation (waveform retrieval, phase picking, hypocentre inversion) is
**deferred from v1** and tracked on the project backlog.

## Options Considered

1. **In v1** — it is the capability that reaches below catalog threshold (ASM-01b) and the
   system's principal differentiator; also by far the largest single body of work specified.
2. **Deferred to backlog (selected)** — v1 delivers the correlation, discrimination,
   confidence, and provenance pipeline over catalog and reporting sources; triangulation
   lands on a foundation that already handles its outputs (REQ-5.11 keeps both solutions
   when it arrives).

## Rationale

The OQ-07 spike materially reduced the urgency: ISC delivers IDC (CTBTO) surface-event
detections at munition scale for the region of interest (ASM-08), so v1 can see its primary
subject matter from catalogs alone. Triangulation remains the path below *that* floor, and
the deferral costs nothing architecturally: the Observation model, coverage model, and
dual-solution rule are already specified to receive it.

## Tracking

Backlog: GitHub issue "Backlog: independent triangulation from raw waveforms".

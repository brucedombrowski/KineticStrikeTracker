# DM-2026-007 — Depth Provenance, and Choosing Formats by What They Disclose

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (issue #23: "sure go ahead", after requesting evidence)
- **Drafted by:** AI agent (Claude)
- **Resolves:** OQ-09
- **Affects:** REQ-5.2 (strengthened), REQ-2.19 and REQ-7.8 (added), ASM-08 and VC-04 (corrected)

## Question

Does an IDC origin published at depth `0.0` mean the depth was *solved to the surface*, or merely
*not solved*? The two readings are opposite in consequence: one makes the tool blind, the other
makes it a false-positive generator over tectonically active ground.

## Evidence

Queried ISC in QuakeML rather than the pipe-delimited text format, same window and box. Committed
at `requirements/evidence/isc-iran-jun2025-quakeml.xml`.

- **All 13 IDC origins carry `<depthType>operator assigned</depthType>`**, with no published depth
  uncertainty. The depth was assigned, not determined.
- Horizontal uncertainty ellipses range **146×312 km to 265×1011 km**.
- Every IDC event carries `typeCertainty` **`suspected`** and publishes no magnitude.
- For contrast, in the same response: NEIC's Semnan origin has a **10×14 km** ellipse and
  `typeCertainty` `known`; ISN's has **6×10 km** with a freely determined 22.3 km depth.

## Decision

**Option (c): read depth provenance from the source rather than inferring it from the value.**
`depthType` answers the question authoritatively per origin, replacing the previous heuristic of
guessing from whether the number looked like a common default.

Three consequences follow, all adopted:

1. **REQ-2.19 (new)** — where a source offers multiple formats, use the one carrying the most
   complete provenance, even at the cost of a harder parser. The ISC adapter moves to QuakeML.
2. **REQ-7.8 (new)** — published uncertainty drives the location confidence axis; unpublished
   uncertainty reports as `unknown`, never as small; uncertainty too large to separate candidate
   sites reports as `unusable`.
3. **REQ-5.2 (strengthened)** — the depth discriminant applies only where the source says the depth
   was solved, and the system records when it declines and why.

## Why this matters beyond the immediate fix

The text format did not lie. It simply omitted three fields, and every omission pointed the same
direction: toward more confidence than the data supports. Consuming it produced an assessment that
looked well-founded and was not — thirteen "surface events at strike magnitude" that are in truth
thirteen poorly-constrained detections spanning a large fraction of a country.

That is the general lesson now encoded in REQ-2.19: **a format that hides the quality of a
measurement is not a cheaper version of one that discloses it. It is a different and worse input.**

## Correction recorded

ASM-08 and VC-04 previously described IDC-via-ISC as "the highest-value single feed for the
mission" and asserted the discriminant pattern was "empirically present in public data." Both were
written from the lossy format and both overstated what the record supports. They are corrected in
REQ-2026-001 v0.5.0 rather than quietly edited: the original claim, the evidence that refuted it,
and the correction all remain visible.

## Consequence for the roadmap

If site-level attribution is a goal, it cannot come from other agencies' published locations —
their uncertainty is the binding constraint, not their availability. This materially strengthens
the case for issue #3, independent triangulation.

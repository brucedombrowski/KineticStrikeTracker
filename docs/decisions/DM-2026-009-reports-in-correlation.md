# DM-2026-009 — What a Report May Contribute to an Event

- **Date:** 2026-09-03
- **Status:** **Approved**
- **Decided by:** Bruce Dombrowski ("approve dm-009, implement r1 and r2")
- **Drafted by:** agent, from investigation of the 2026 Strait of Hormuz campaign
- **Resolves:** issue #29 · **Affects:** REQ-7.3 (amend), REQ-6.7 (proposed), REQ-7.9 (proposed)

## Problem

Non-instrumental observations (classes B–E) are currently treated as co-equal positional
evidence: they join the same transitive-closure clustering as instrumental detections, and
contribute to position, location confidence, and classification on equal terms.

The result is that **adding curated reports manufactures high-confidence "surface-explosion"
events out of gas flares.**

## Evidence

| Case | Constituents | System reports |
|---|---|---|
| Bandar Abbas flare, two pixels of **one** overpass | 2 × firms [A], 1 distinct source | occurrence = **high** |
| Larak + *Sidr* + flare field | 38 × firms [A], 2 × seed [C] | occurrence = high, location = high, `[surface-explosion]` |
| Bushehr report, no instrumental support | 1 × seed [C], instrumental 0 | `[surface-explosion]` |

Larak and *Sidr* are **77.1 km and ~1 h apart** and were merged into one event positioned
46.6 km from the first and 120.8 km from the second — outside both constituents' own stated
uncertainty.

**Row 1 needs no seed data and is live today.** It is not an artefact of the campaign
dataset; every FIRMS run currently reports two pixels of one flare as high confidence that an
event occurred.

## Three mechanisms

1. **Bridging.** `same_physical_event` (`analysis.cpp:215-224`) selects `reporting_km = 100.0` /
   `reporting_seconds = 86400.0` whenever *either* side is non-instrumental. Clustering is
   transitive, so one coarse report chains together every instrumental detection within
   100 km and 24 h, and then anything within reach of those.
2. **Minimum-uncertainty inheritance.** `analysis.cpp:372-377` keeps the *smallest*
   uncertainty in the group, so a ±60 km report merged with a sub-kilometre pixel reports
   `location=high`. This inverts REQ-7.8.
3. **Observation-counting mistaken for source-counting.** `instrumental_sources` is
   incremented **per observation** (`analysis.cpp:361`) but consumed as a count of
   independent sources (`occurrence_band`, `analysis.cpp:29`). The code's own comment three
   lines above states the intent it violates: *"independence is counted by distinct source id
   (REQ-7.3)."* `independent_sources` does this correctly; `instrumental_sources` does not.

## Options considered

**Option 1 — Tighten the reporting window.** Reduce `reporting_km` / `reporting_seconds`.
*Rejected.* Reports genuinely carry coarse time and position; any window narrow enough to stop
bridging is too narrow to match real reports, trading false positives for false negatives.
It also leaves mechanisms 2 and 3 untouched.

**Option 2 — Attachment, not merger.** Reports may attach to an instrumentally formed
cluster but may never form or bridge one. *Viable, but incomplete on its own:* it does not say
what happens when a report matches several clusters, and picking the nearest asserts a
correspondence the evidence does not establish.

**Option 3 — Attachment with ambiguity (recommended).** Option 2, plus an explicit
rule that a report matching more than one cluster corroborates none of them.

**Option 4 — Exclude non-instrumental sources from analysis.** *Rejected.* REQ-7.2 defines
classes B–E with distinct weights and REQ-2.18 mandates the curated seed dataset; this would
gut the corroboration model the requirements call for.

## Decision (recommended)

**R1 — Event formation is instrumental-only.** Transitive association runs over class-A
observations. `reporting_km` / `reporting_seconds` no longer participate in cluster formation.

**R2 — Reports attach, and ambiguity is recorded.** Each non-instrumental observation is
matched against the formed clusters within the reporting window:

| Matches | Outcome |
|---|---|
| 0 | stands alone as a **reported-only** event, located by its own uncertainty |
| exactly 1 | attaches as corroboration |
| more than 1 | corroborates **none**; recorded as **ambiguous**, candidates listed |

**R3 — Corroboration counts distinct sources.** `instrumental_sources` counts distinct
instrumental `source_id`, not observations, per REQ-7.3.

**R4 — Each axis derives from what supports it.** `best_location_uncertainty_km` minimises
over positional evidence only; an attached report neither sharpens nor widens it. An event
with no class-A constituent may not carry an instrumental classification —
it is `reported-only`, never `surface-explosion`.

## Why ambiguity rather than nearest-match

A report placing a strike on Qeshm on 20 July, against five candidate clusters on Qeshm that
day, corresponds to at most one of them. Choosing the nearest fabricates a correspondence;
attaching to all five inflates all five. **Corroborating none is the only answer the evidence
supports**, and it is the one the tool exists to give (REQ-11.4).

This is the same reasoning as DM-2026-008's one-way constraint: where a rule could move a
verdict in either direction, it may only move it toward the more conservative reading.

## Determinism (REQ-1.2)

All four rules are order-independent. R2 matches against clusters that are themselves a pure
function of membership (REQ-6.3), so the match set is a pure function of the input snapshot.
Ambiguity is decided by cardinality, not by iteration order — no tie-break is required, which
is the point of the rule.

## Consequences

- Occurrence bands fall across existing output wherever a cluster held several observations
  from one source. This is a correction, not a regression.
- Larak and *Sidr* separate. Both have instrumental clusters within the matching window, so
  both become **ambiguous → reported-only**: a known strike with confirmed satellite coverage
  and no attributable instrumental detection. That is the intended validation result and the
  measurement the seed dataset was added to obtain.
- Verification artefacts and any committed report fixtures need regeneration.
- `data/seed/hormuz-2026.json` becomes usable for published reporting; until then it should
  not be.

## Implementation status

| Rule | State |
|---|---|
| R1 — instrumental-only event formation | **implemented** |
| R2 — attach with ambiguity | **implemented** |
| R3 — corroboration by distinct source | **implemented** |
| R4 — per-axis derivation | **not implemented** |

**R3** was taken first, separately: it was a defect against REQ-7.3 as written rather than a
change to it. Events at `occurrence=high` fell from 1969 to 16 over the Hormuz window.

**R1 and R2** then removed the two remaining false highs. Reports no longer enter the
association relation, so none can bridge unrelated detections; each is matched against the
frozen instrumental partition and attaches only on a unique match. Over the Hormuz window the
nine curated entries resolve as **6 reported-only** and **3 ambiguous**, none attached.

Larak is now the case the dataset was added to obtain:

```
2026-08-30T23:00:00.000Z   [surface-explosion]
  position (derived): 26.850, 56.360
  confidence: occurrence=low  location=high  characterisation=low
  independent sources: 1 (instrumental 0)
  report association:
    * seed:larak-island-2026-08-30 ambiguous — matches 7 instrumental clusters
      within the reporting window, so corroborates none. Candidates: ...
```

Its own time, its own position, its own ±5 km, and an explicit record of the seven gas-flare
clusters it declined to claim as corroboration. A known strike with confirmed satellite
coverage and no attributable instrumental detection — a measured point on the detection floor.

Emitting the verdict required a new `Event::report_association`, carried in both the text
report and the GeoJSON. The pre-existing `association_reasons` remains unemitted: it holds one
line per associated pair, so a 38-member flare cluster produces hundreds, and turning it on
wholesale would be a separate decision about report volume.

**R4 remains open**, and is now the most visible gap: a reported-only event still carries
`[surface-explosion]`, as the Larak block above shows. The label asserts a characterisation no
instrumental evidence supports. `best_location_uncertainty_km` also still minimises across all
constituents, which matters whenever a report does attach.

## Proposed requirement changes

- **REQ-7.3 (amend)** — state that instrumental corroboration is counted by distinct source
  identity, closing the gap between the requirement and `instrumental_sources`.
- **REQ-6.7 (new)** — non-instrumental observations shall not participate in event formation
  and shall not associate two otherwise-unassociated observations.
- **REQ-7.9 (new)** — where a report matches more than one candidate event it shall
  corroborate none, and the ambiguity shall be recorded and emitted.

## Residual question

The `reporting_km = 100.0` / `reporting_seconds = 86400.0` values were chosen for cluster
formation. Under R1–R2 they govern only *matching*. They are retained unchanged here because
no evidence yet bears on what they should be for that narrower job; this is flagged for
revisit once the campaign dataset has been run against the corrected analysis, and is not
part of the decision requested.

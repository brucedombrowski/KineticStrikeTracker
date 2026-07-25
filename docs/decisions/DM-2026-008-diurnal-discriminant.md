# DM-2026-008 — Diurnal-Regularity Discriminant

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (issue #25: "your recommendation is fine, implement it")
- **Resolves:** OQ-10 · **Affects:** REQ-5.12 (added)

## Decision

Identify industrial blasting from its **working-hours signature**, derived from the data,
rather than only from the manually maintained site registry of REQ-5.5.

**Rule:** 0.1° cells; ≥30 explosion-typed events; ≥90% inside the busiest 10-hour window;
≥6 empty hours; **and the window centre must fall in daylight local solar time (05:00–19:00),
derived from longitude.**

**One-way only:** may reclassify to `industrial-blast`, may never promote to
`surface-explosion`.

## Evidence

| | S. Jordan phosphate belt | Gaza Strip |
|---|---|---|
| n | 1,152 | 1,425 |
| Active hours | 06:00–13:00 UTC only | all 24 |
| Peak | 09:00–10:00 UTC (midday local) | 23:00, 02:00 UTC |
| Empty hours | 14 of 24 | 0 |

Industry keeps working hours. War does not.

## The daylight constraint, and why it was necessary

The first implementation required only a *tight* window, not a *daytime* one. It
reclassified 1,773 events — sweeping in Gaza cells whose events cluster tightly at night.
**A rule that relabels sustained night bombardment as quarry work is worse than no rule at
all**, so the window centre is now converted to local solar time via longitude (no timezone
database, nothing to go stale) and must fall in daylight.

Post-fix: Gaza **0** events misclassified; S. Jordan 962 correctly identified.

## Why one-way

A facility struck repeatedly at similar hours could mimic the signature. The rule may
therefore only move a verdict toward the more conservative classification. Its reasoning is
recorded in full like every other discriminant (REQ-5.6).

## Relationship to REQ-5.5

Complementary, not a replacement. The registry encodes known sites; this derives unknown
ones. A detected cell is also a **candidate site for the registry**, pending confirmation by
imagery or other review.

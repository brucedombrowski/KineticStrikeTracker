# DM-2026-010 — When a Window Counts as Unobserved

- **Date:** 2026-09-03
- **Status:** **Approved**
- **Decided by:** Bruce Dombrowski ("approve dm-010")
- **Drafted by:** agent, while fixing issue #27
- **Resolves:** issue #27 (criterion 2) · **Affects:** REQ-1.6, REQ-8.6, REQ-2.15

## Problem

A source that subdivides its request can return data for some sub-intervals and nothing for
others. NASA's `VIIRS_SNPP` stream returned **zero rows for 11-15 July and 3 August 2026**
across the whole Gulf, while the run reported `coverage complete`. The hole coincided with the
largest strike wave in the window, so a query at those targets returned an "absence" that was
really an ignorance.

The fix needs a rule for when to stop claiming coverage. Issue #27 proposed one:
*"`coverage_complete` is false when any requested sub-interval returned no data."* Implementing
it literally turned out to be wrong, and this memorandum records the substitution.

## Options considered

**Option 1 — Any empty sub-interval marks coverage incomplete.** The issue's literal criterion.
*Rejected.* Most sub-intervals in most regions are legitimately empty: a five-day chunk over
quiet terrain returns nothing because nothing burned, not because nobody looked. This rule
makes `INCOMPLETE` the permanent state of every quiet region, and a flag that is always on
carries no information. It would invert REQ-8.8's purpose — the point is that unobserved must
not read as quiet, not that quiet should read as unobserved.

**Option 2 — Internal inconsistency (selected).** Coverage is incomplete when a source returned
data for **at least one** sub-interval and **none** for at least one other. Uniform emptiness
is `no_data`: the source answered, and there was nothing in range.

**Option 3 — Rely on multi-product agreement alone.** Query several products and treat a chunk
as covered if any returned data, without flagging anything. *Rejected as insufficient on its
own* — it repairs the common case silently and says nothing when every product is dark.
Adopted **in addition to** Option 2, not instead of it.

## Decision

Coverage is incomplete when a source's sub-intervals disagree with each other. A source
answering for twelve chunks and silent for two has a hole in a window it was otherwise
answering; that is evidence about the source, not about the region.

Uniform silence is left as `no_data`, which the model already distinguishes from failure.

## What this rule cannot see

**An outage spanning the entire requested window.** With no answered sub-interval to contrast
against, uniform silence is indistinguishable from a quiet region using one source alone. This
is a real limit, and it is the reason Option 3 was adopted alongside: querying VIIRS and MODIS
together (REQ-2.15) means a single satellite's outage is visible as disagreement *between
products* even when it covers every chunk.

Both sources dark for a whole window remains undetectable. Narrowing that further would need a
climatological expectation of detection rate per region, which the project does not have and
should not invent.

## Consequences

- Schema v3 adds `coverage_interval`, persisted rather than held in memory so a report and an
  offline replay (REQ-2.11) can state coverage from stored data alone.
- The text report and GeoJSON name unobserved intervals explicitly.
- FIRMS queries VIIRS_SNPP and MODIS together by default, satisfying REQ-2.15, which had been
  only half met. They are merged under one source id because they share an upstream origin and
  must not count as independent corroboration (REQ-7.3, DM-2026-009 R3).

## Verification

`tests/test_coverage.cpp` pins the rule: a hole mid-window is a gap; a fully answered window is
not; a uniformly empty window is `no_data`; multiple holes are all reported in canonical order.
A committed fixture holds the header-only CSV that started this, asserting it still parses to
zero observations without error — the adapter was never wrong about that body, which is why the
gap had to be recorded elsewhere.

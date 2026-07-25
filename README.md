# Kinetic Strike Tracker

A deterministic, open-source tool that correlates objective geophysical observations with
open-source reporting to identify and characterise candidate kinetic strike events, initially
over Iran and surrounding areas.

**Status:** Phase 1 — requirements definition. No implementation yet.

---

## What it is

The system ingests observations from independent public sources — seismic catalogs, space-based
thermal anomaly detections, official government statements, curated conflict datasets, and news
reporting — normalises them into a common event model, correlates them in space and time, applies
explicit seismic discrimination rules, and produces candidate event assessments with graded
confidence and full provenance.

Every assertion traces back to the source records that support it.

## What it is not

It is not a targeting system, not a real-time warning system, and not an authoritative record of
fact. It is a retrospective analytical tool operating on already-published reports of events that
have already occurred. It produces *candidate assessments with stated confidence*, never verified
determinations. See requirement category 11 for the full scope exclusions, which are recorded as
requirements precisely so that they remain auditable and cannot drift through feature accretion.

## Design commitments

**Deterministic.** Given a frozen input snapshot and a configuration, every run produces
byte-identical output. No unseeded randomness, no wall-clock dependence in analysis, no
unordered-container iteration on an output path. This is what makes results reproducible and
auditable, and it forbids opaque statistical classification in the analysis core.

**Provenance-complete.** Raw source responses are retained append-only with digests. Any analysis
can be replayed offline against a stored snapshot with no network access.

**Honest about what it cannot see.** See below.

**Standard library first.** C++20, relying on the standard library and OS-provided libraries
(libcurl, libsqlite3). Third-party runtime dependencies require a recorded decision memorandum.

## The detection floor — read this before trusting any output

The public global seismic catalog cannot resolve kinetic strikes over Iran. This was measured, not
assumed.

A query of the USGS FDSN event service covering 2025-06-01 to 2025-07-05 over a box containing
Fordow, Natanz and Isfahan — spanning the entire June 2025 campaign, **with no minimum magnitude** —
returned two events, neither at any struck site. The 22 June strike on Fordow produced no USGS
catalogued event at all. A wider regional query returned nothing below M4.1.

Munition-scale events couple into the ground at roughly M2.0–M2.5. USGS completeness over Iran is
around M4.0. The gap is two to three orders of magnitude in energy.

Two consequences shape the whole design:

1. **Regional networks are mandatory**, not optional. Only catalogs with low enough magnitude of
   completeness can see these events at all.
2. **Independent triangulation is the differentiator.** Catalog origins arrive already located;
   consuming them cannot reach below the threshold at which someone else decided to publish. Picking
   arrivals across stations and inverting for a hypocentre is the only route into the sub-threshold
   regime where most strikes live.

Consequently: **absence of a detection is not evidence that no strike occurred**, and every report
the system produces is required to say so.

The evidence behind this section is committed under [`requirements/evidence/`](requirements/evidence/)
and the cases derived from it are recorded as validation cases in the requirements document.

## Repository layout

```
requirements/
  REQ-2026-001.json     Requirements specification (authoritative, machine-readable)
  evidence/             Source snapshots supporting stated assumptions
docs/
  decisions/            Decision memoranda, DM-YYYY-NNN
  verification/         Requirement → implementation → test traceability
```

## Process

Developed under the [systems-engineering](https://github.com/brucedombrowski/systems-engineering)
process framework: requirements capture, implementation, decision documentation, verification, and
version-control traceability. Requirements use RFC 2119 keywords, carry stable IEEE 29148
identifiers, cite a governing standard, and name a verification method.

Development is open. Planning sessions are archived to the issue tracker as transcript records.

## Data sources

| Source | Class | Role |
|---|---|---|
| USGS FDSN event service | A — instrumental | Global catalog; regional coverage insufficient (see above) |
| EMSC SeismicPortal | A — instrumental | Independent European-Mediterranean catalog |
| Regional networks (IRSC, AFAD/KOERI) | A — instrumental | Low-Mc regional coverage — the only catalogs that reach strike magnitudes |
| NASA FIRMS (VIIRS / MODIS) | A — instrumental | Space-based thermal anomaly detections; sees fire-producing strikes with no seismic signal |
| USCENTCOM public releases | B — government | Official statements |
| ACLED | C — curated dataset | Human-coded conflict events with published methodology |
| News media | D — reporting | Corroboration only; never sole basis for a high-confidence assessment |
| Social media | E — unattributed | Lowest weight; treated as adversary-influenced input |

Confidence rises with genuinely *independent* corroboration. Sources sharing an upstream origin —
twenty outlets carrying one wire report — count once.

## Licence

[Apache-2.0](LICENSE) — selected per [DM-2026-002](docs/decisions/DM-2026-002-license-selection.md)
for its express patent grant and retaliation clause, suited to a publicly developed tool in a
defence-adjacent domain.

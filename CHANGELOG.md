# Changelog

All notable changes to this project are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- **The ISC adapter was reading a lossy format** (issue #23). The FDSN pipe-delimited text
  format omits `depthType`, `originUncertainty`, and `typeCertainty`; every omission pointed
  toward more confidence than the data supports. Adapter now reads QuakeML (REQ-2.19).
- **Location confidence reported `moderate` for origins uncertain to ±850 km.** Published
  uncertainty now drives the location axis; unpublished uncertainty reports `unknown`, never
  small; uncertainty too large to separate candidate sites reports `unusable` (REQ-7.8).
- **Depth discriminant guessed provenance from the value.** It now reads `depthType` from the
  source and records when it declines and why (REQ-5.2 strengthened).
- **The FIRMS MAP_KEY was being persisted into the database** (REQ-10.6). Request URLs are
  stored as provenance, and the key travels in the FIRMS request path, so every `raw_response`
  row held a live credential. Nothing was ever committed — `data/*.db` is gitignored and no
  database is tracked — but a database handed to a collaborator, or a bug report quoting a
  stored URL, would have carried it. Credentials are now stripped from URLs and from error text
  before either is stored or emitted, leaving the request legible and reissuable with the
  reader's own key. `redact_secret` is exported and unit-tested rather than left as an internal
  convenience, because REQ-10.6 is a hard constraint and ought to be verifiable.
- **Fourteen retrievals were being recorded as one** (issue #28). The FIRMS adapter walks a long
  window in 5-day chunks, and every chunk collapsed into a single `raw_response` row whose
  `request_url` named only the last chunk, whose `requested_at` came from the first, whose
  `http_status` was hardcoded to 200, and whose `sha256` digested a concatenation NASA never
  served — so the stored response could not be verified against the source, which is the point
  of content-addressing it. Contrary to REQ-2.10 and REQ-4.6.
  The adapter interface now carries a `Retrieval` per request, so this is fixed for any future
  chunking adapter and not only for FIRMS. Each is persisted with its own URL, timestamp and
  status; bodies remain content-addressed, so re-fetching unchanged content adds a retrieval row
  and no body. Nothing is concatenated any more — each chunk is parsed on its own, which is also
  what lets `observation.raw_response_id` name the exact response an observation came from
  (REQ-7.4). That column existed since schema v1 and had never been populated.
- **A six-day hole inside a requested window was reported as complete coverage** (issue #27,
  DM-2026-010, approved). NASA's `VIIRS_SNPP` stream returned zero rows for 11-15 July and 3 August 2026
  across the whole Gulf; `coverage_complete` tracked adapter success, not whether the data
  returned spanned the window asked for, so the run printed `coverage complete` over the days of
  the largest strike wave in the period. Adapters that subdivide a request now report a coverage
  interval per sub-request, persisted in schema v3 so a report — and an offline replay
  (REQ-2.11) — can state it from stored data alone. Coverage is incomplete when a source
  answered for some sub-intervals and not others; uniform silence stays `no_data`, because
  flagging every quiet region would make the warning permanent and meaningless (DM-2026-010).
  The CLI, the text report and the GeoJSON all name unobserved intervals.
- **FIRMS queried one satellite when REQ-2.15 requires two.** `make_firms` defaulted to
  `VIIRS_SNPP_NRT` and the CLI never overrode it, so MODIS was never ingested and one
  satellite's outage blinded the whole run. VIIRS_SNPP and MODIS are now queried together by
  default and merged under one source id — they share an upstream origin and must not count as
  independent corroboration (REQ-7.3). `--firms-product` selects families explicitly. Over
  01-20 July 2026 the single-product run reports `coverage INCOMPLETE` and names the five-day
  hole; the default run covers it, and records that VIIRS_SNPP was the empty one.
- **Reports built with `--offline` silently dropped their detection limitations.** `cmd_report`
  constructed a `probe` adapter set for exactly this purpose and then never used it, so every
  `replay` produced a report with no limitations section and no attributions — contrary to
  REQ-8.6 and REQ-2.12. Constructing an adapter touches no network; only `fetch()` does.
  Coverage recorded by an earlier ingest is also now surfaced even when the reporting run did
  not configure that adapter, so a stored gap cannot be dropped by invocation shape.
- **A report of a strike was being characterised as an observation of one** (DM-2026-009 R4).
  An event with no instrumental constituent inherited `surface-explosion` from the reporting
  source's own event type — the label asserted a characterisation nothing instrumental
  supported. Such events now carry a new `reported-only` classification, and the withheld
  verdict is recorded in the discrimination reasoning rather than silently dropped. Location
  confidence likewise now derives from instrumental constituents where any exist, so an
  attached report can neither sharpen nor widen an instrumentally located event, and an event
  located only by a report keeps that report's own stated bound. `reported-only` has its own
  colour in the map viewer; rendering it as `indeterminate` grey would have made an
  unsupported report look like an instrumental detection that could not be classified (REQ-8.8).
- **A single coarse report could bridge unrelated detections into one event** (issue #29,
  DM-2026-009 R1/R2, approved). Non-instrumental observations joined the same transitive
  association relation as instrumental ones, over a 100 km / 24 h window — so one report chained
  38 gas-flare pixels and two reported strikes 77 km apart into a single "high confidence
  surface explosion" positioned 47 km from one and 121 km from the other. Event formation is now
  instrumental-only; a report is matched against the frozen instrumental partition and attaches
  only when it matches exactly one cluster. Matching several corroborates none — the report
  corresponds to at most one, so choosing the nearest would assert a correspondence the evidence
  does not establish (REQ-11.4). The verdict is emitted as `report_association` in both the text
  report and the GeoJSON. Over the Hormuz window the nine curated entries resolve as 6
  reported-only and 3 ambiguous, and Larak Island becomes a known strike with confirmed
  satellite coverage and no attributable detection — the measurement the dataset was added for.
- **Two pixels of one gas flare reported high confidence that an event occurred.**
  `instrumental_sources` and `reporting_sources` counted observations, while `occurrence_band`
  and `characterisation_band` consumed them as counts of independent sources — so a single
  satellite overpass detecting one flare twice read as two corroborating instrumental sources.
  The comment three lines above the increment stated the rule the code broke: independence is
  counted by distinct source id (REQ-7.3). Now counted that way, per DM-2026-009 R3. Over the
  Hormuz validation window this drops events asserted at `occurrence=high` from **1969 to 16**:
  14 genuine ISC+USGS pairs on the same earthquake, and 2 that remain wrong for a different
  reason still open as issue #29. Output remains byte-identical across runs (REQ-1.2).
- **The curated-seed format could not express how well an entry was located.** The parser read
  every field of the observation model except `location_uncertainty_km`, so a named 5 km island
  and a 120 km island were indistinguishable in precision. Absent that field REQ-7.8 correctly
  reports `unknown`, but the format gave a curator no way to state a bound they genuinely had.

### Changed
- **ASM-08 and VC-04 corrected.** Both were written from the lossy text format and overstated
  what the record supports — the claim that IDC-via-ISC is "the highest-value single feed" does
  not survive the QuakeML evidence. Corrected in place with the refuting evidence committed,
  not quietly edited (DM-2026-007).
- **Scope language scrubbed** (issue #21): the region of interest is Earth. Iran and June 2025
  appear only as validation cases and as a CLI demo default, never as a scope boundary —
  requirements, README, CLAUDE.md, and CLI help all updated.

### Added
- `kst::xml` — bounded read-only XML parser. Refuses DOCTYPE and all entity declarations
  outright (billion-laughs and XXE vectors), honours only the five predefined entities and
  numeric character references, bounded depth/size/name/text/children (REQ-12.3, REQ-12.9).
- `requirements/evidence/isc-iran-jun2025-quakeml.xml` — the QuakeML record behind DM-2026-007.
- Schema v2: `depth_type` and `type_certainty` columns.
- `data/seed/hormuz-2026.json` — nine curated observations from the 2026 Strait of Hormuz
  campaign (REQ-2.18): Larak Island, the six targets CENTCOM named on 14-15 July, Jask, Qeshm,
  and the tanker *Sidr*. Each carries a stated location uncertainty (±5 km for a small island,
  ±60 km for Qeshm), an explicit date-placeholder flag where only a date is published, and
  casualties and attribution as source-attributed statements only (REQ-11.5). Known strikes at
  known times and places are better validation data than the June 2025 set because the
  detection floor can be *measured* against them rather than asserted.
  **Not yet usable for published reporting — see DM-2026-009 and issue #29.**
- `docs/decisions/DM-2026-009-reports-in-correlation.md` (**draft, pending review**) — what a
  non-instrumental report may contribute to an event. Recommends instrumental-only event
  formation, attach-with-ambiguity for reports, corroboration counted by distinct source, and
  per-axis derivation of confidence. Proposes amending REQ-7.3 and adding REQ-6.7 / REQ-7.9.

### Changed
- REQ-2026-001 v0.3.1: `open_questions` now holds ONLY unresolved items so the count is
  meaningful and drivable to zero (currently **0**); the seven answered questions move to
  a `resolved_questions` archive with their resolutions. Viewer reports "none outstanding"
  and collapses the archive.
- REQ-2026-001 v0.3.0 (issue #20, DM-2026-005): RFC 9562 replaces obsoleted RFC 4122
  (pinned to name-based UUIDv5 — time-ordered variants excluded for determinism),
  ISO/IEC 25010:2023 and NIST SP 800-53 Rev. 5 pinned, RFC 8446 added as TLS baseline
  (1.2 min / 1.3 preferred), REQ-13.6 SBOM-at-release added. OpenAPI and C++23 rejected
  for v1 with recorded rationale. 104 requirements, 17 standards.

### Changed
- REQ-2026-001 v0.3.1: `open_questions` now holds ONLY unresolved items so the count is
  meaningful and drivable to zero (currently **0**); the seven answered questions move to
  a `resolved_questions` archive with their resolutions. Viewer reports "none outstanding"
  and collapses the archive.
- REQ-2026-001 bumped to v0.2.0 applying the human review received as issue #2 — all seven
  open questions resolved. REQ-1.1 (whole-Earth model), REQ-2.4 (ISC as the seismic
  aggregation route; direct IRSC excluded by its no-redistribution licence), and REQ-9.6
  (CMake approved as build-time tool) amended; REQ-1.6 (coverage model), REQ-2.18 (curated
  seed dataset), REQ-7.7 (representation-agnostic confidence), REQ-8.8 (coverage grey-out
  layer) added — 103 requirements total.

### Added
- **Working prototype** (issues #9-#12): end-to-end `ingest -> analyse -> report`.
  Observation model with RFC 9562 UUIDv5 deterministic identity and self-contained
  ISO 8601/civil-date handling (REQ-3.x); pluggable adapters for ISC (CTBTO IDC route),
  USGS GeoJSON, and local files/curated seed data (REQ-2.1/2.2/2.4/2.13/2.18);
  order-independent union-find correlation with cross-catalog dedup (REQ-6.3/6.5);
  auditable discrimination rules that decline to fire on agency-default depths
  (REQ-5.2/5.6); confidence stored as factors with derived ordinal bands on three
  separate axes (REQ-7.5/7.7); GeoJSON + text report with mandatory detection-limitation
  statement and coverage notes (REQ-8.2/8.3/8.6).
  Verified live: 16 observations from ISC+USGS over the June 2025 Iran window,
  15 candidate events, byte-identical across repeated runs (REQ-1.2).
- Content-addressed raw-body storage per DM-2026-006 (REQ-4.6).
- SQLite persistence layer `kst::db` (issue #8): versioned schema with ordered forward
  migrations, refusing databases newer than the build (REQ-4.2); `raw_response`
  append-only enforced by SQL triggers rather than convention (REQ-4.5); idempotent
  observation upsert on the natural key (REQ-4.3); prepared statements with bound
  parameters throughout, no interface to splice values into SQL (REQ-12.2). Schema
  carries run metadata (REQ-4.4), revision history (REQ-2.17), fixed-depth flag (REQ-5.2),
  source class (REQ-7.2), curated-seed flag (REQ-2.18) and file digests (REQ-12.10).
- HTTPS retrieval layer `kst::http` (issue #7) over OS-provided libcurl 8.7.1 and
  CommonCrypto — no third-party dependency (REQ-9.4). TLS verification always on with no
  interface to disable it, TLS 1.2 floor per RFC 8446 (REQ-10.5); https-only with
  redirects barred from downgrading, bounded response size, redirect count and timeouts
  (REQ-12.8); SHA-256 digest and UTC request timestamp on every response (REQ-2.10).
  Network confined to this layer (REQ-9.3). Tests run offline (REQ-10.4).
- Bounded JSON parser `kst::json` (issue #6): strict RFC 8259, in-project per the
  stdlib-first posture (REQ-9.4). Explicit documented limits on input size, depth,
  string length, and member count (REQ-12.3); full UTF-8 and surrogate validation;
  duplicate keys rejected; object members kept in document order — no unordered
  containers (REQ-1.2). Adversarial test suite per REQ-12.9 plus the committed USGS
  evidence fixture as a real-world case (REQ-10.4).
- Build skeleton (issue #5): CMake project, `kst_core` static library separated from the
  thin `kst` CLI (REQ-9.2), stdlib-only test harness registered with CTest (REQ-10.2),
  warnings-as-errors set documented in the root CMakeLists (REQ-9.7). CLI verbs from
  REQ-8.1 present as honest stubs (exit 2, not fake success).
- Phase 2 backlog seeded as issues #5–#19, each traced to its requirements.
- `requirements/evidence/isc-iran-jun2025.txt` — ISC FDSN query result (OQ-07 spike):
  thirteen IDC (CTBTO) depth-0.0 origins clustered on June 2025 strike nights, basis for
  ASM-08 and validation case VC-04.
- Decision memoranda DM-2026-001 (CMake), DM-2026-003 (confidence representation),
  DM-2026-004 (triangulation deferred), all decided by human review issue #2.
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

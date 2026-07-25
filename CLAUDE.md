# KineticStrikeTracker — Agent Instructions

## Project

Deterministic open-source tool correlating public geophysical observations with open-source
reporting to identify candidate kinetic strike events **anywhere on Earth**. Scope is the planet: the data model is
whole-planet, analyses are scoped by a configurable region of interest supplied at run time, and coverage is explicit — regions we can't see render as unknown, not
quiet (REQ-1.1, REQ-1.6, REQ-8.8). Iran/June 2025 appears only as validation data, never as a scope
boundary. Sources are pluggable adapters, added as desired/available
(REQ-2.1). C++20, macOS arm64, local-first, public repository.

Read `requirements/REQ-2026-001.json` before proposing any change. It is authoritative.

## Governing process

This project follows the [systems-engineering](https://github.com/brucedombrowski/systems-engineering)
framework at `~/systems-engineering`. Phases: requirements capture → implementation → decision
documentation → verification → version control and traceability.

Core principle: **AI drafts, human reviews.** Requirements and decisions are drafts until Bruce
approves them.

## Hard constraints

These are requirements, not preferences. Violating one is a defect.

- **Determinism (REQ-1.2).** Same input snapshot plus same config produces byte-identical output.
  No unseeded randomness. No wall-clock in analysis. No iteration over unordered containers on any
  output path. No opaque or trained classifiers in the analysis core.
- **Stdlib first (REQ-9.4).** C++ standard library plus OS-provided libraries (libcurl,
  libsqlite3, libz). A third-party runtime dependency requires a decision memorandum first.
- **All input is hostile (REQ-12.x).** Every source, including local files. Parameterised SQL only.
  Bounded parser depth and size. Never execute or shell out with source-derived content. Escape for
  the output context — CSV formulae, HTML, terminal control sequences.
- **Public repository (REQ-10.6).** No credentials, ever. ACLED and FIRMS keys come from
  environment or `config.local.*`, which is gitignored.
- **Scope exclusions (REQ-11.x).** No targeting functionality. No tracking of individuals. No
  authoritative claims of fact — only candidate assessments with stated confidence.
- **Honest reporting (REQ-8.6).** Absence of detection is never reported as absence of event. The
  detection floor is real and measured; see README.

## Conventions

- Requirement IDs are stable. Never renumber — deprecate instead.
- Decisions go in `docs/decisions/DM-YYYY-NNN`, minimum two options considered, rationale recorded.
- Verification traceability in `docs/verification/`.
- CHANGELOG.md updated with every substantive commit. Keep a Changelog format, SemVer.
- Timestamps UTC, ISO 8601, always. Coordinates WGS 84 decimal degrees.
- Magnitude types are never silently converted between networks.

## "Close up shop" procedure

When Bruce says to close up shop, the session transcript is published to the GitHub issue tracker
as the planning meeting record (REQ-13.2):

1. Source transcript: `~/.claude/projects/-Users-brucedombrowski-KineticStrikeTracker/<session-id>.jsonl`
2. **Review it first (REQ-13.3)** — the repository is public. Scan for credentials, personal
   information, private third-party data, and absolute paths that expose unrelated local content.
   Review is a precondition of publication, not a follow-up.
3. Render to readable Markdown — human direction and agent output clearly distinguished. Do not
   dump raw JSONL.
4. Publish via `gh issue create`, labelled `human-prompt` / `agent-output` / `decision` per Phase 5
   of the process framework.
5. Report to Bruce what was redacted, if anything.

## Open questions — one per GitHub issue

Every open question in the requirements document gets its **own** GitHub issue, labelled
`question` + `human-prompt`. Never bundle multiple questions into one issue.

The loop:
1. Question arises during work → add to `open_questions` in REQ-2026-001.json → file it as
   its own issue with options and a recommendation → **tell Bruce explicitly to answer it**.
2. Bruce answers on the issue (or via the viewer's markup → file review).
3. Apply: amend/add requirements, record a DM if it is a design decision, move the question
   to `resolved_questions` with its resolution, close the issue.

`open_questions` holds only unresolved items; the count is meant to return to zero.

## Known open questions

Tracked in the `open_questions` block of the requirements document. As of the initial draft: build
system (Makefile vs CMake), licence selection, region-of-interest extent, whether news/social
ingestion is in v1 scope, confidence representation (numeric vs ordinal), whether independent
triangulation is v1 scope, and whether IRSC offers a machine-readable interface.

That last one is the highest-risk unknown — REQ-2.4 is mandatory and load-bearing, and its
implementation route is unverified.

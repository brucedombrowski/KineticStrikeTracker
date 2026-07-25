# DM-2026-006 — Raw-Response Retention

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (issue #22: "your recommendation is fine")
- **Drafted by:** AI agent (Claude)
- **Resolves:** OQ-08
- **Affects:** REQ-4.6 (added)

## Decision

**Content-addressed deduplication**, adopted unconditionally. Each distinct raw response
body is stored once keyed by its SHA-256; every retrieval still records its own request
URL, timestamp, and HTTP status referencing that body.

## Options Considered

1. **Unbounded append.** Simplest, most literal reading of REQ-4.5. Rejected as the sole
   policy: whole-Earth scope plus repeated polling grows without limit, and nothing in the
   requirements acknowledged the cost.
2. **Age-based export to external files.** Keeps the working database lean and preserves
   provenance via retained digests. **Deferred, not rejected** — it composes with the
   selected option and should be decided against measured volume rather than guessed.
3. **Content-addressed dedup (selected).** Invisible to callers, loses no information.

## Rationale

Polling an unchanged time window returns byte-identical bodies, so the same content is
otherwise stored many times over. Dedup removes that redundancy without discarding
anything.

The load-bearing distinction: **the body is deduplicated; the retrieval is not.** Knowing
*that* a fetch happened, and when, is provenance (REQ-7.4) and is independent of whether
the bytes differed from last time. Collapsing repeated retrievals into one record would
destroy the ability to say when a source was last confirmed unchanged — which is precisely
the evidence needed to distinguish "no events" from "not checked" under the coverage model
(REQ-1.6).

Append-only (REQ-4.5) is unaffected: bodies are still never modified or deleted.

## Revisit

Option 2 returns to the table once a full year of multi-source ingestion has been measured.

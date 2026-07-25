# Method: investigating a reported strike

How an incident goes from a news report to a defensible finding. Nothing here requires
privileged access — every step is something an analyst could do by hand. The tool exists to
make the steps repeatable, auditable, and honest about their limits.

## The workflow

```
reported incident  →  establish time & place  →  encode as curated observation
                                                          ↓
                                          query instrumental sources for that
                                          window and region
                                                          ↓
                            correlate  →  corroborated / uncorroborated / contradicted
                                                          ↓
                                            record the result either way
```

### 1. Establish time and place

From open reporting. Two things matter more than the narrative: **the date and the
coordinates**, and **how precisely each is known**. A report saying "Tuesday, in the north
of the city" is a different input from one naming a school with a street address.

Record what you could not establish. A missing time of day is not a small gap — the
diurnal discriminant (REQ-5.12) and the association window (REQ-6.2) both depend on it.

### 2. Encode it as a curated observation

Write the incident into a seed file (`data/seed/*.json`, REQ-2.18). It enters the system as
**source class C — curated**, is flagged `is_curated`, and carries the curator's attribution.
It is never silently promoted to the standing of an instrumental measurement.

```json
[{ "id": "...", "time": "...Z", "latitude": 0.0, "longitude": 0.0,
   "description": "what was reported, and by whom",
   "event_type": "explosion", "author": "human-curated from <sources>" }]
```

Then: `kst ingest --seed data/seed/known-events.json --offline`

### 3. Query the instrumental record

Same window, generous region — generous because published locations can be uncertain by
hundreds of kilometres (see [DM-2026-007](decisions/DM-2026-007-depth-provenance-and-format-choice.md)).

`kst ingest --start ... --end ... --bbox S,N,W,E`

### 4. Read the correlation honestly

Three outcomes, and **all three are results**:

| Outcome | Meaning |
|---|---|
| **Corroborated** | An instrumental observation associates in space and time. Confidence rises because the sources are independent (REQ-7.3). |
| **Uncorroborated** | The curated event stands alone: `independent sources: 1 (instrumental 0)`. This is *not* evidence the event did not occur. |
| **Contradicted** | Instrumental data supports a different explanation — e.g. a natural earthquake where a strike was alleged. Recorded as disconfirming evidence (REQ-7.6). |

### 5. Ask why, when uncorroborated

The uninteresting answer is that nothing happened. It is usually the wrong one. Check, in
order:

1. **Does any agency publish explosion-typed events there?** This is the dominant factor and
   it is editorial, not physical. Israel's GSI publishes them; most national networks do not.
2. **Is the event above the local detection threshold?** Munitions couple at roughly
   M2.0–M2.5; global catalogue completeness over much of the world is near M4.0 (ASM-01).
3. **Is the published location precise enough to associate?** A ±850 km ellipse cannot be
   tied to a building.
4. **Was the energy coupled into the ground at all?** Air bursts — including interceptions —
   largely are not.

## What this method cannot do

- **It cannot confirm an incident occurred.** Corroboration raises confidence; absence
  establishes nothing (REQ-8.6).
- **It cannot attribute responsibility.** Out of scope by requirement (REQ-11.5).
- **It cannot count casualties.** Where a source reports them, they are carried as an
  attributed quotation, never derived.
- **It cannot locate to a building.** Public seismic locations are kilometres-to-hundreds-of-
  kilometres uncertain. Naming a specific structure requires imagery or ground reporting,
  not this tool.

## Why the negative results are the valuable ones

A tool that only ever confirms is a tool that cannot be trusted when it confirms. The
uncorroborated cases are what calibrate it — they measure where the instrumental record is
silent, and *why*. See [case studies](case-studies/) for worked examples, including one where
a well-documented, UN-condemned incident produced no detection whatsoever.

The honest framing for any output of this system: **it reports what the public instrumental
record contains, not what happened.**

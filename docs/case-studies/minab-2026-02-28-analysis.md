# Why the system has no evidence of the Minab school strike

**Incident:** Shajareh Tayyebeh girls' primary school, Minab, Hormozgan province, Iran,
28 February 2026. Reported struck three times; ~165–175 killed, predominantly schoolchildren.
Condemned by UN human rights experts and UNESCO.

**System result:** no instrumental detection.

**Finding: the strike was undetectable with the instrumentation available, by a margin of
roughly 60× in energy. This is a physical limit, not an analytical failure — and not evidence
about whether the incident occurred.**

Every figure below is independently checkable. Commands, coordinates, and sources are given
so a reader can reproduce or refute each step.

---

## 1. Coordinates used

| | Value | How to check |
|---|---|---|
| Minab (town centre) | **27.147 N, 57.080 E** | Any mapping service; town-level, not the school |
| Nearest open seismic station (II.UOSS) | **25.28 N, 55.70 E** | IRIS station metadata, below |
| Distance between them | **260 km** | Paste both pairs into Google Maps / any great-circle calculator |

**The school's exact position was not established.** Open reporting names the school and town,
not a street address or coordinates. Town-centre coordinates are therefore used, and this is
recorded as a limitation rather than papered over: a town-level position is adequate for the
detection analysis (which turns on hundreds of kilometres) but would be inadequate for any
claim about a specific building.

*Verify the distance yourself:* `27.147, 57.080` → `25.28, 55.70`. Any tool should return
≈260 km.

---

## 2. What instrumentation exists near Minab

Queried the open FDSN station federation for every station within ~1,330 km:

```
curl "https://service.iris.edu/fdsnws/station/1/query?net=*&latitude=27.147&longitude=57.080&maxradius=12&level=station&format=text"
```

| Radius | Real open stations |
|---|---|
| 100 km | **0** |
| 200 km | **0** |
| 300 km | **1** |
| 500 km | **1** |

The single station within 500 km is **II.UOSS**, University of Sharjah, UAE, at **260 km**.
The next-nearest are a temporary array at 501 km. (A `SY.UOSS` entry also returns from the
service; it is *synthetic* — a modelled station, not an instrument — and is excluded.)

Two consequences, both decisive:

1. **One station cannot locate anything.** A hypocentre requires arrival times at three or
   more stations (REQ-5.9). With one, no location is possible at any magnitude.
2. **260 km is far** for a small surface event. Regional detection thresholds degrade sharply
   with distance.

Full station list: [`data/reference/seismic-stations-hormuz.json`](../../data/reference/seismic-stations-hormuz.json).
Toggle "reference layer" and "distance rings" in the event map to see this geometrically.

---

## 3. How large a seismic signal would the strike have produced?

Yield-to-magnitude for chemical explosions, using
`mb ≈ 0.75·log₁₀(W tons TNT-equiv) + C`, with C ≈ 2.6 for a surface burst and ≈ 4.0 for a
fully-coupled buried shot:

| Munition | HE mass | mb, surface burst | mb, buried |
|---|---|---|---|
| Tomahawk TLAM-E (WDU-36) | 454 kg | **2.34** | 3.74 |
| GBU-31 JDAM (Mk84) | 429 kg | 2.32 | 3.72 |
| GBU-12 (Mk82) | 87 kg | 1.80 | 3.20 |
| GBU-57 MOP | 2,400 kg | 2.89 | 4.29 |

**The surface-burst column is the relevant one.** A weapon striking a building detonates at
or just above ground; most energy goes into blast, fragmentation and heat, and only a small
fraction couples into the crust. The buried column applies to a charge emplaced underground
and is included only to show how much coupling matters — the same warhead differs by 1.4
magnitude units, a factor of ~25 in seismic energy, depending on emplacement.

A triple-tap adds at most `0.75 · log₁₀(3) ≈ 0.36` magnitude units, and only if the impacts
were simultaneous. They were reported as sequential, so each would register separately.

**Expected: mb ≈ 2.3–2.7.**

*These are order-of-magnitude estimates.* Coupling varies with geology, burst height, and
soil, and published constants differ between studies. The conclusion below survives an error
of a full magnitude unit in either direction.

---

## 4. What the catalogue actually resolves there

From our own ingested data — five months, whole Hormuz region:

| Measure | Value |
|---|---|
| Observations, 2026-02-26 → 2026-07-25 | **54** |
| Typed `explosion` | **0** |
| Typed `earthquake` | **54** |
| Smallest magnitude catalogued | **M3.5** |
| Events below M3.5 | **0** |

Evidence: [`requirements/evidence/hormuz-2026-feb-jul.csv`](../../requirements/evidence/hormuz-2026-feb-jul.csv)
(SHA-256 `759de828…`). Reproduce with:

```
kst ingest --db /tmp/hormuz.db --start 2026-02-26T00:00:00Z --end 2026-07-25T23:59:59Z --bbox 24,28.5,53,59
```

**The empirical detection floor is M3.5.** Not one event smaller than that was catalogued
anywhere in the region across five months.

---

## 5. The gap

```
expected signal   mb ≈ 2.3
detection floor   M  ≈ 3.5
gap               ≈ 1.2 magnitude units  ≈  60× in energy
```

The strike was roughly **sixty times too small** to enter the catalogue, in a region with
**zero stations within 200 km** and **no possibility of location** even had it been detected.

---

## 6. On the date itself

The entire region produced one catalogued event on 28 February 2026:

| Time (UTC) | Agency | Type | Mag | Position | Uncertainty | Distance from Minab |
|---|---|---|---|---|---|---|
| 13:07:58 | IDC | earthquake | 3.7 mb | 26.815 N, 54.921 E | ±82 km | **~215 km** |

The correlation engine correctly declined to associate it with the curated event: 215 km
exceeds every association window (REQ-6.2), and it is typed as an earthquake. The system
reported the incident as `independent sources: 1 (instrumental 0)`.

---

## 7. Ruling out the alternative explanations

| Hypothesis | Verdict |
|---|---|
| Agencies detected it but chose not to publish an `explosion` type | **Not the binding constraint here.** It is decisive in Iran generally — every one of 54 events is typed `earthquake` — but with no stations inside 200 km there was nothing to publish. |
| The event was too small | **Confirmed.** mb ≈ 2.3 against an M3.5 floor. |
| Location precision was inadequate | **Confirmed, and moot.** Regional IDC origins carry ±52–83 km uncertainty; a single station yields no location at all. |
| Energy never coupled to the ground | **Contributing.** Surface burst couples poorly — the 1.4-unit surface/buried difference above. |

---

## 8. What would have been needed to see it

1. **Local stations.** Three or more within ~50 km would put mb 2.3 comfortably above
   threshold and permit location. Israel's GSI achieves exactly this over Gaza, which is why
   the same tool finds 4,800+ explosion-typed events there and none here.
2. **Waveform-level processing** rather than catalogue consumption (issue #3). With enough
   stations, template matching can reach below the catalogue threshold — but it cannot conjure
   stations that do not exist.
3. **Non-seismic instrumental sources.** NASA FIRMS thermal detections (REQ-2.15, issue #13)
   see fires from orbit regardless of ground instrumentation or any agency's editorial choice.
   **This is the highest-value gap in the system today.**

---

## 9. What this does and does not establish

**Establishes:** the public seismic record cannot see events of this size in this region, and
the system correctly reported nothing rather than manufacturing a detection.

**Does not establish:** anything whatsoever about whether the incident occurred, its cause, or
responsibility. The instrumental record is silent because it is deaf here, not because the
ground was quiet. Casualty figures above are carried as reported, never derived (REQ-11.5).

This is the reference case for REQ-8.6 — *absence of a detection is not evidence that no
strike occurred* — and it is why that sentence is required in every report the system emits.

---

## 10. Reproduce this analysis

```bash
# instrumental record for the region and window
kst ingest --db /tmp/hormuz.db --start 2026-02-26T00:00:00Z \
           --end 2026-07-25T23:59:59Z --bbox 24,28.5,53,59

# the human-defined event
kst ingest --db /tmp/hormuz.db --offline --seed data/seed/known-events.json

# report, then open tools/event-map.html and enable the reference layer
kst report --db /tmp/hormuz.db --bbox 24,28.5,53,59 --out out
```

Station query, verbatim:

```bash
curl "https://service.iris.edu/fdsnws/station/1/query?net=*&latitude=27.147&longitude=57.080&maxradius=12&level=station&format=text"
```

## Open gap: imagery

Before/after satellite imagery would materially strengthen this dossier — it is the one line
of evidence that does not depend on ground instrumentation. It is **not** included here
because acquiring 10 m Sentinel-2 or commercial-resolution imagery requires registered API
access that this project does not yet hold. Openly available MODIS/VIIRS imagery is 250 m–1 km
per pixel, far too coarse to resolve a school building. Tracked as a gap rather than
substituted with something inadequate.

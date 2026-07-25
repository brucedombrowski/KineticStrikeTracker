# Reference layers

Named locations overlaid on the event map for context. Generic by design — the same
schema serves seismic stations, industrial sites, and any other point of interest.

```json
[ { "name": "II.UOSS", "lat": 25.28, "lon": 55.70,
    "category": "seismic station", "note": "Univ. of Sharjah, UAE" } ]
```

`category` drives the marker style and the toggle. `km` is optional and, when present,
is distance from whatever the layer was generated relative to.

**Reference layers are context, never attribution.** Proximity between an event and a
named site is not evidence that the site was involved, and given published location
uncertainties of tens to hundreds of kilometres (DM-2026-007) adjacency is frequently
meaningless. The map shows the uncertainty ellipse alongside any proximity so the two
are read together (REQ-8.7, REQ-11.4).

## Layers

| File | Contents | Provenance |
|---|---|---|
| `seismic-stations-hormuz.json` | 40 nearest open-federation seismic stations to Minab, Iran | IRIS/EarthScope FDSN station service, 2026-07-25 |

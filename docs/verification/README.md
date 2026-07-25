# Verification

Requirement traceability per Phase 4 of the
[systems-engineering](https://github.com/brucedombrowski/systems-engineering) process framework.

Each requirement in `requirements/REQ-2026-001.json` maps forward to its implementing source
location and its verifying test:

```
requirement → file → line → verification method → evidence
```

Verification methods: inspection, test, analysis, demonstration.

Empty until implementation begins. The verification matrix (`mapping.json`) is generated from the
requirements document and maintained alongside implementation, not retrofitted at the end.

## Validation cases

Three validation cases are defined in the requirements document, derived from the June 2025 Iran
campaign, with supporting evidence committed under `requirements/evidence/`:

- **VC-01** — June 2025 strikes on nuclear facilities. Tests that insufficient source coverage is
  reported as such, not as absence of an event.
- **VC-02** — Semnan M4.9, 2025-06-20. Tests that class A instrumental evidence outweighs class D
  press speculation, and that the conflict between them is recorded rather than silently resolved.
- **VC-03** — Fixed-depth artefact. Tests that agency default depths are not mistaken for
  measurements by the depth discriminant.

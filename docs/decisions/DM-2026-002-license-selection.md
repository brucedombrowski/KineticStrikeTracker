# DM-2026-002 — Open Source Licence Selection

- **Date:** 2026-07-25
- **Status:** Approved
- **Decided by:** Bruce Dombrowski (human directive, planning session 2026-07-25)
- **Drafted by:** AI agent (Claude), per process principle 1 (AI drafts, human reviews)
- **Resolves:** OQ-02
- **Satisfies:** REQ-13.1 (OSI-approved licence, SPDX-identified)

## Decision

The project is licensed under **Apache-2.0**.

## Options Considered

### Option 1 — Apache-2.0 (selected)

- Express patent grant from every contributor (§3), with a patent-retaliation clause terminating
  the grant of any party that litigates over the covered work.
- Requires preservation of NOTICE/attribution and statement of changes — consistent with this
  project's provenance-first posture.
- Well understood by institutional and government users, relevant for a publicly developed tool
  in a defence-adjacent domain.

### Option 2 — MIT

- Shorter and maximally permissive, but grants no express patent rights and offers no
  retaliation protection.
- Rejected: the patent posture matters more here than brevity.

## Rationale

The project is developed openly in a domain where patent exposure is plausible and institutional
reuse is hoped for. Apache-2.0's express patent grant and retaliation clause protect both
contributors and downstream users at negligible cost. The attribution and change-statement
obligations align with requirements already levied on the system itself (REQ-2.12, REQ-8.5).

## Implementation

- Canonical licence text at `/LICENSE` (SHA-256
  `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`, matching the published
  https://www.apache.org/licenses/LICENSE-2.0.txt).
- SPDX identifier: `Apache-2.0`.
- README licence section updated; OQ-02 disposition updated in REQ-2026-001.

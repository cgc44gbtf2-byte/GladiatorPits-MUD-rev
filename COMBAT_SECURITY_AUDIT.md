# Combat, memory and security audit

## Baseline and incremental plan

Start from `master` commit `cd38d0efb33468aceab7cd448bf19e209f3882f1`.
All 27 local source files match that baseline; no open issues or PRs were
returned by the repository queries. Native CI is passing and required on
`master`; this audit will use a separate draft PR.

1. Reproduce combat continuing after a fighter is marked for disconnect,
   including failed output during attack messages. Preserve ordinary surrender
   wins/losses rather than allowing a pending disconnect to become permadeath.
2. Add deterministic tests for hit thresholds, damage, recovery and the existing
   strictly-greater-than-health death rule. Audit the time-seeded combat RNG;
   retain D10 rules while removing predictable randomness if reproduced.
3. Check orderly-shutdown allocation ownership and credential-buffer cleanup,
   without inventing surrender outcomes or changing saved records on shutdown.
4. Check the combat heartbeat's dependence on the wall clock, retaining its
   one-pulse-per-iteration behavior and ordinary one-second cadence.
5. Run the full suites after each increment, then both compilers' optimized and
   sanitizer configurations, bounded analyzers, and actual GitHub CI.

No live accounts, records, production processes or deployments are accessed.
Preserve stats, training, damage formulas, hit thresholds, arenas, progression,
permadeath, permissions and the historical native player-record ABI. No rewrite.

## Findings, changes and verification

- Increment 1 reproduced `pending disconnect cannot inflict a lethal hit before
  surrender cleanup`: the combat pulse ran before the loop closed connections
  already marked for revocation/expiry or failed output. A departing fighter
  could kill, or be killed, instead of receiving ordinary surrender cleanup.
- Combat now skips both ends of a pending disconnect before advancing its
  timers and again before committing damage, since attack-message enqueue can
  itself discover a failed/slow socket. Actual completed hits retain normal
  outcomes; unread socket EOF is still discovered by normal input processing.
- Six heap-owned lifecycle cases cover either endpoint marked closing,
  either endpoint marked write-failed, and either endpoint failing during
  attack-message output. Records survive, surrender awards exactly one win
  and loss with no kill, and both connections/bodies are released afterward.
- Strict optimized GCC passes 514 game/runtime + 120 account checks and both
  TCP suites after increment 1.
- Increment 2 first failed the production-dice replay regression: resetting
  libc's public seed reproduced the same 64-roll sequence. The server had
  seeded that generator with wall-clock seconds. D10 now uses the already
  required libsodium's uniform cryptographic generator; startup no longer seeds
  libc for combat. Range, dice count, hit thresholds and damage rules are
  unchanged, but random streams intentionally differ. There was no seeded
  replay interface or persisted RNG state to preserve.
- A test-only dice override makes rule tests deterministic; its fallback and
  the randomness/range tests call the real production roller. Coverage includes
  miss/first and last light hit/first and high heavy hit, odd integer half-damage,
  ninth-pulse first attacks, speed-plus-three recovery, both turn orders,
  damage exactly equal to health remaining alive, next-hit permadeath,
  persisted victory counters and defeated-body cleanup. The seed-independence
  comparison has a theoretical 10^-64 collision probability; it is not an
  entropy-quality proof.
- Strict optimized GCC passes 637 game/runtime + 120 account checks and both
  TCP suites after increment 2.
- Increment 3 found that normal server shutdown left live connection/body
  allocations rooted in the global list until process exit. This is retained
  ownership, not a demonstrated unreachable leak: leak sanitizers can consider
  globally reachable allocations live. Added explicit shutdown destruction and
  a shared storage destructor that wipes full connection/body allocations before
  freeing them, including queued input/output and credential buffers.
- Shutdown deliberately does not call ordinary surrender settlement or save
  unfinished combat/training: the previous restart semantics and existing
  record bytes are preserved. The worker is joined before shutdown storage
  destruction. Normal disconnect still settles and saves as before.
- A fixture with two fighters, an anonymous client, partial/password buffers
  and three actual socket pairs verifies empty ownership/descriptor sets,
  closed client descriptors, byte-identical pre-fight records and idempotent
  cleanup. Strict GCC passes 659 game/runtime + 120 account checks and both
  TCP suites after increment 3.
- Increment 4 first failed the heartbeat clock-domain regression. Combat had
  used adjustable wall-clock seconds, so a backward clock correction could
  suspend turns until wall time caught up. It now uses the existing monotonic
  clock, and compares timestamps without signed subtraction.
- Pulse cadence still allows at most one update per loop iteration, records
  its timestamp after the update, and does not replay missed turns after lag.
  Tests cover equal/forward/backward/unavailable samples and signed 64-bit
  boundaries where applicable. No system clock was changed during testing.
- Strict optimized GCC passes 665 game/runtime + 120 account checks and both
  TCP suites after increment 4.

## Final verification

GCC 15.3.0 and Clang 20.1.8 optimized and ASan/UBSan entries all pass
**665 game/runtime + 120 account/security checks and both real-TCP suites**.
The shared CI entry point builds both production binaries with strict warnings
and `-Werror`. Sanitizer entries enable leak detection and halt on undefined
behavior; no findings occurred in exercised paths. No unreachable leak is
claimed to have been reproduced merely from the prior global ownership.

GCC's analyzer and bounded Clang analysis complete without diagnostics on all
four native translation units. Runs are sequential, capped at 35 seconds and
384/512 MiB virtual memory respectively; Clang exploration is limited to 5,000
nodes per top-level analysis. These bounds limit path coverage.

Native ABI and persisted-record regressions pass, historical CRLF is retained,
and whitespace checks pass. Actual hosted results will be recorded in the PR
description without modifying the tested source.

The first final-matrix attempt passed GCC optimized and sanitizer entries, then
Clang rejected an implicit unsigned-enum-to-int conversion in a new test's
assertion under `-Werror`. Replaced that assertion argument with an explicit
comparison; no gameplay conversion or warning suppression was introduced.

## Coverage limits

This is bounded source review and fixture-based verification, not an exhaustive
memory/security certification. Retain the TLS, ABI, filesystem crash/disk-full,
production-load/DoS and broader-platform limitations in prior audit logs.

## Follow-up cleanup: challenge admission

Continue from tested PR #6 head
`d614aa5f82df4c7c56a7015f1fc740343dea06b1`. All 28 local source files match
that head before this increment; PR #6 is still draft with no review/issue
comments or competing open PRs. `master` is unchanged.

Plan: reproduce whether a pending-close challenger can still be accepted and
gain an artificial surrender result when cleanup runs. Reject new challenge
admission for either closing/write-failed participant, including output failure
while sending challenge/acceptance messages. Preserve ordinary live duels,
existing pending challenge cleanup, counters and player-record bytes. Add
heap-owned lifecycle regressions and rerun the shared compiler/sanitizer matrix
and hosted CI before updating the draft. Do not merge or modify required checks.

The first regression failed with `late challenge/accept cannot fabricate a
surrender win or loss`: accepting a closing challenger changed both endpoints
to fighting, so ordinary cleanup persisted a win/loss for a fight that should
never have started. Both handlers now check pending-close/write-failed state
before admission and after relevant output operations. Acceptance also checks
after the global arena announcement, whose enqueue can itself fail.

Fourteen heap-owned cases exercise either endpoint already closing/write-failed,
either endpoint failing on an individual message, and either endpoint failing
only on the acceptance broadcast. They verify no fabricated counters, unchanged
damage/speed/room and pending state, preserved saved characters and full cleanup.
GCC optimized passes 805 game/runtime + 120 account checks and both TCP suites.

Next, reproduce the equivalent admission window for an account whose session
has been revoked but has not yet been marked closing by periodic polling.

The second regression failed with `revoked or stale challenger cannot
manufacture a surrender win before polling`. A live body/status alone did not
establish that the target still had a current account session. Admission now
rechecks both participants' current `PERM_PLAY` permission, alongside transport
state, before linking challenges and before committing acceptance.

Four additional rejection scenarios cover challenge/accept against a disabled
account and an account re-enabled with a stale epoch. They preserve records and
produce no fabricated surrender counters. A positive case obtains a fresh
session after re-enablement and verifies that an ordinary duel still starts.
No roles, record layouts, formulas or active-duel settlement rules were changed.
GCC optimized passes 858 game/runtime + 120 account checks and both TCP suites
after this second increment.

Final follow-up verification: all four local GCC/Clang optimized and ASan/UBSan
entries pass **858 game/runtime + 120 account/security checks and both TCP
suites**, with strict warnings and leak detection enabled and no sanitizer
findings in the exercised paths. Both bounded analyzers complete without
diagnostics on all four production translation units, using the limits recorded
above. Historical CRLF and whitespace checks pass.

Completed hosted-run/job logs will be verified and recorded in PR #6 against
the new head; the earlier 665-check hosted run does not verify this follow-up.
Filesystem/crash failure handling remains a separate, explicitly unverified
limitation; this admission cleanup does not claim to fix it.

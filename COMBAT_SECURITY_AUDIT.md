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

## Coverage limits

This is bounded source review and fixture-based verification, not an exhaustive
memory/security certification. Retain the TLS, ABI, filesystem crash/disk-full,
production-load/DoS and broader-platform limitations in prior audit logs.

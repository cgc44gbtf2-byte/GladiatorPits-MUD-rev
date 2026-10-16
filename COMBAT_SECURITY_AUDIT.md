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

## Coverage limits

This is bounded source review and fixture-based verification, not an exhaustive
memory/security certification. Retain the TLS, ABI, filesystem crash/disk-full,
production-load/DoS and broader-platform limitations in prior audit logs.

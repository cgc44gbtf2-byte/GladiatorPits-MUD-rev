# Post-merge reliability fixes

## Baseline and plan

PRs #1, #2 and #3 were merged, in that order, into `master`. The merged commit
`fefb7d6f32d9c6e421af78525e8fc026bac284f7` has exactly the previously tested
23-file source tree. GitHub reported no check runs or reviews; the merge did
not bypass a failing check or rewrite commit history. Branches were retained.

Continue in a separate follow-up PR:

1. Test whether late input can revive an expired authentication prompt between
   periodic timeout sweeps. Enforce expiry at input-processing boundaries,
   invalidate pending requests and wipe partial credential input.
2. Exercise the remaining unchecked arena-ID increment at integer boundaries.
   Keep ordinary positive arena allocation, but avoid overflow and collisions
   with occupied rooms when wrapping.
3. Run regressions after each increment, then strict optimized/ASan/UBSan
   GCC/Clang suites and bounded native analyzers. Record failures below.

Preserve all existing gameplay, account permissions and native player-record
fields. No live data is accessed or migrated. No new deployment is initiated.

## Verification and failure log

- Increment 1: the new late-input regression first failed with
  `late input cannot queue work or revive any expired authentication stage`.
  A deadline could expire while the event loop was in `select`; a subsequent
  password/confirmation line could reset the deadline or queue work before
  the next timeout sweep. This required no wrong password to be accepted,
  but violated the intended authentication timeout.
- Added input-time expiry checks before socket data/complete lines and before
  authentication state transitions. Expiry closes the connection, invalidates
  the request and wipes partial input, buffered passwords and staff metadata.
  Completed responses also sample the clock freshly at their own expiry check,
  rather than relying on a timestamp captured before processing other results.
- Strict optimized GCC passed 441 game/runtime + 120 account/security checks
  and both TCP suites after increment 1. Coverage exercises every pending stage,
  inclusive expiry, partial/batched/oversized wire input, no queued late work,
  and credential/metadata clearing. Existing live-deadline flows remain covered.
- Increment 2: moved the private arena cursor to file scope solely to exercise
  its boundary through the real `accept` command. The otherwise unchanged
  increment first triggered UBSan signed overflow at `INT_MAX`.
- Arena allocation now advances without signed overflow, wraps to positive
  IDs, skips every occupied room, and has a descriptor-bounded search. A failed
  search rejects acceptance before changing health, speed, room or challenge
  state. The cursor is transient; no player-record field or layout changes.
- GCC ASan/UBSan with recovery disabled passed 458 game/runtime + 120 account
  checks and both TCP suites. New tests cover the upper boundary, wrapping,
  multiple occupied arenas and unchanged existing fights. A synthetic world
  exceeding normal descriptor admission exercises defensive search exhaustion;
  that fixture does not imply normal games have a 1,024-arena limit.

## Final verification

- GCC 15.3.0 and Clang 20.1.8 optimized production builds and ASan/UBSan builds
  all pass **458 game/runtime + 120 account/security checks and both real-TCP
  suites**, with strict warnings and `-Werror`. Sanitizers enable leak detection
  and halt on undefined behavior. No sanitizer findings occurred in exercised
  paths. The existing historical native ABI and persisted-data checks still pass.
- GCC's analyzer and Clang's bounded analyzer complete without diagnostics on
  all four native translation units, including the command/Telnet sources
  included by `glad.c`. Runs are sequential with 35-second per-unit time limits
  and virtual-memory caps; Clang is limited to 5,000 exploded nodes per
  top-level analysis. These bounds restrict path coverage.
- Historical CRLF conventions and incremental whitespace checks pass. No new
  runtime dependency, schema migration, credential reset, ownership transfer,
  player-data mutation or gameplay-system removal is part of this follow-up.
- These checks ran in the development environment, not a newly configured
  GitHub CI workflow. Future automatic CI remains separate work.

## Remaining limits

The release limits in SECURITY_FIXES.md still apply: external TLS is required,
native saves are ABI-dependent, and production-load/fuzz/crash/other-platform
coverage is not established. These changes are not a production security
certification.

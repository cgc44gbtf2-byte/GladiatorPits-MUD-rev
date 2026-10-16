# Post-dashboard security and compatibility fixes

## Plan recorded before implementation

The audit of dashboard commit `07d63dadd4f69d29ee5dd370bcfbc3289a4b36fc`
reproduced four defects using disposable accounts and records. Implement and
test these in order, without changing the native player-record ABI:

1. Bind each queued authentication result to a unique request as well as its
   connection. Cancellation must invalidate the outstanding request. Test a
   cancelled staff proof arriving during another action and stale login results.
2. Check deadlines before accepting every kind of completed authentication
   result. Test success/failure at expired and live deadlines.
3. Replace unchecked win/loss/kill increments with checked, saturating increments
   at `INT_MAX`. Preserve existing values and file layout; log the ceiling rather
   than wrapping negative or silently resetting progression. Test combat,
   surrender, persistence and ordinary increments.
4. Parse Telnet control framing separately from application lines, retaining
   bounded parser state across reads. Consume negotiations/subnegotiations and
   reject unsupported options without feeding their payload into commands or
   passwords. Test fragmentation, escaped IAC, line controls and real TCP login.

For each increment, first demonstrate a failing regression, then run the full
native/account/TCP suite after the fix. Finish with strict GCC/Clang optimized
and ASan/UBSan builds and bounded static analysis. Record failures, results and
remaining limitations below. Update draft PR #3 only; do not merge or deploy.

## Baseline audit evidence

- 175 game/runtime and 120 account checks plus both TCP suites passed with
  GCC 15.3.0 and Clang 20.1.8 under ASan/UBSan.
- GCC's analyzer completed without diagnostics for all four native translation
  units. An unbounded Clang analyzer run was interrupted by a workspace restart;
  that run is not a pass and its cause was not established.
- A cancelled Administrator grant applied during a newer Moderator request.
  This required a legitimate Owner password proof; it did not demonstrate a
  Player-to-Owner permission bypass.
- An expired login result granted a session before the timeout sweep.
- UBSan reported signed overflow in the surrender win increment at `INT_MAX`.
- A valid Telnet NAWS frame for an 80-by-24 terminal contaminated `who`.

## Incremental verification

- Increment 1: the new cancellation/replacement test first failed with
  `cancelled staff proof cannot mutate or cancel a newer action`, reproducing
  the audit finding. Added a process-unique request token alongside the
  connection token. Cancellation clears the expected request; mismatched/zero
  results are discarded without changing the newer flow. Token exhaustion
  fails closed instead of wrapping. Worker code still never accesses a
  connection or commits a staff action.
- After increment 1, strict optimized GCC passed 203 game/runtime + 120 account
  checks and both TCP suites. Coverage includes cancelled staff replay, stale
  successful/failed logins, matching prompted identity and token exhaustion.

## Release boundary

All fixtures are disposable. No real accounts, character records, deployment
configuration or running production services are modified. Existing gameplay,
permadeath, permissions, stats and native body layout remain in scope to preserve.
Plain TCP still requires an external TLS gateway or private tunnel.

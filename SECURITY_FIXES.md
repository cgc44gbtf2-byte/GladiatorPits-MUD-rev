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
- Increment 2: the new inclusive-deadline matrix failed on a completed generic
  authentication result before the fix. All matching results now check the
  deadline before resetting prompt state or accepting a session/action.
  Successes and failures at expired deadlines close the connection; live
  results retain their existing behavior. Strict optimized GCC then passed
  254 game/runtime + 120 account checks and both TCP suites.
- Discarding a late/cancelled response cannot undo an account registration or
  password change the worker has already committed. Such results do not grant
  a session. Staff mutations are different: the worker performs only password
  proof, and the main-thread commit requires the current request and deadline.
- Increment 3: the new combat ceiling regression first triggered UBSan signed
  overflow at the win award. Both combat win/kill awards and surrender win/loss
  awards now use checked increments. A counter at `INT_MAX` retains that exact
  value and logs its ceiling; it is not reset or rewritten into a new layout.
  GCC ASan/UBSan with undefined-behavior recovery disabled then passed
  271 game/runtime + 120 account checks and both TCP suites. New coverage checks
  `INT_MAX-1` to `INT_MAX`, repeated awards at the ceiling, surrender, loadable
  saved progression and unchanged permadeath.
- Increment 4: the new valid-NAWS test first failed because a width byte became
  part of `who`. Added a bounded streaming Telnet control-frame parser before
  line processing. It retains only parser state across reads, consumes
  subnegotiation payloads/escaped IAC, refuses unsupported WILL/DO options, and
  accepts the acknowledgment of existing password ECHO announcements.
  WONT/DONT acknowledgments do not trigger reply loops. Existing printable
  ASCII filtering, application-line limits and CRLF behavior remain intact.
- GCC ASan/UBSan with undefined-behavior recovery disabled then passed
  342 game/runtime + 120 account checks and both TCP suites. Added coverage
  includes every option byte under all four negotiation verbs, NUL option
  replies, bytewise fragmentation, control commands, oversized subnegotiations,
  embedded newlines/quit text, escaped IAC, password non-disclosure and
  unchanged password bytes. Real TCP registration/login and staff password
  proofs also exercise NAWS and password echo acknowledgments; plaintext
  clients remain covered separately.
- One multi-file patch initially missed the smoke-test print-line context;
  that file was unchanged by the failed hunk and was patched with the actual
  context before verification. No runtime or authorization failure was bypassed.

## Final verification

- GCC 15.3.0 and Clang 20.1.8: optimized production builds and ASan/UBSan
  builds with all strict warnings plus `-Werror` pass **342 game/runtime +
  120 account/security checks and both TCP suites**. Sanitizer runs enable
  leak detection and halt on undefined behavior. No sanitizer findings remain
  in these exercised paths; this is not proof that all memory bugs are absent.
- GCC `-fanalyzer` and Clang's analyzer complete without diagnostics on
  `glad.c` (including command/Telnet sources), `accounts.c`, `player_record.c`
  and `account_admin.c`. To avoid repeating the interrupted unbounded run,
  analysis is sequential, wall-time/virtual-memory limited, with Clang's
  per-top-level analysis bounded to 5,000 exploded nodes. These bounds limit
  path exploration and are not an exhaustive verification claim.

## Release boundary

All fixtures are disposable. No real accounts, character records, deployment
configuration or running production services are modified. Existing gameplay,
permadeath, permissions, stats and native body layout remain in scope to preserve.
Plain TCP still requires an external TLS gateway or private tunnel.

Telnet terminal-size values and terminal-type payloads are intentionally unused.
This is framing/negative negotiation support, not a full RFC 1143 option
negotiator, binary terminal, line editor or tested production-client matrix.
Native counters cannot represent values above `INT_MAX`; extending them needs
a separate backward-compatible persistence design. These fixes do not claim
full fuzz coverage, production-load/DoS resistance, crash/disk-full recovery,
all-platform portability or a production security sign-off.

# Backdoor and cleanup follow-up

## Scope and non-destructive plan

Baseline: draft PR #6 at `8f78ebc9dc4a94c80a93781784ef73f7edbd5b99`;
master at `cd38d0efb33468aceab7cd448bf19e209f3882f1`.
All 28 published files matched the local baseline before investigation.
PR review/issue comments and other open PRs were checked for existing fixes.
No real player records, deployed configuration or production credentials were used.

Review command dispatch, authentication, roles, staff confirmation, local operator
access, network/process execution and build/CI entry points. Then reproduce and
fix individual defects, running regressions after each increment:

1. Reject completed authorization on failed-output connections, including at the
   staff mutation boundary. Test healthy, closing and output-failed connections.
2. Allow the local operator to demote an already-disabled owner without weakening
   last-active-owner protection. Test revocation, re-enablement and audit behavior.
3. Verify privileged-looking account names grant no special authority; replace
   fixed dummy-hash input with ephemeral random material as security hygiene.
4. Preserve the C99 language setting when callers override build optimization
   flags; the first test command exposed a modern-compiler build failure.
5. Run the complete GCC/Clang optimized/sanitizer matrix and bounded analyzers;
   append tested changes to the existing draft PR and verify hosted CI.

## Backdoor review findings

- The original `ce27ba812f79466cc7daa7f01a287f84272901ea` shutdown handler
  compared its argument against a hardcoded shared passphrase. Its value is
  intentionally not reproduced here. Both current master and the baseline PR
  already require `PERM_SHUTDOWN`; the old literal is absent from the 28 current
  files. This is a historical credential gate, not a newly discovered active
  bypass or evidence of malicious intent.
- Current commands use an explicit command/permission table; shutdown also checks
  permission inside its handler. Roles come from current enabled account/session
  metadata, not character names, titles, IP addresses or first registration.
- The offline `glad-admin` utility can bootstrap owners and link legacy records.
  This is documented, intentional OS-operator authority, not a network command.
  Account database opening requires a suitably owned directory and owner-private,
  regular database file. Anyone controlling the server OS user/files remains
  inside this trust boundary.
- No process-execution/dynamic-loading calls or private-key/token-pattern hits
  were found in the published files. The only production `getenv` call configures
  the bind address. This targeted search complements source review; it is not an
  exhaustive secret scanner or dependency/supply-chain certification.
- The pre-existing fixed input used for the nonexistent-user timing hash is not
  a default credential: login additionally requires a real enabled account and
  current session. Randomizing that input is hygiene, not remediation of a proven
  login bypass.
- Local obsolete `libgladstaff.so` and cached Python dashboard bytecode are not in
  the published tree or the native build/runtime entry points. They were not
  executed, deleted or published; their binary contents are outside this review.
  The supported staff dashboard remains in-game, not a web console.

## Reproduction, modifications and verification

### Increment 1: failed-output authorization

Reproduced: `FAIL: closing or failed-output staff proof cannot mutate roles`.
A full nonblocking socket send buffer and bounded output queue mark the connection
`bWriteFailed` while its peer remains connected. A completed, valid staff proof
could still mutate a role before ordinary connection cleanup. This required a
previously authorized staff action; it was not an unauthenticated privilege grant.

`AccountPoll` now discards completed results for failed-output connections as it
already did for closing connections. `StaffApply` independently rejects either
flag before probing the socket or touching account metadata. Completed queue slots
are still wiped. Worker-side registration/password changes that already committed
are not rolled back by discarding their replies.

Twelve completed-result cases cover registration, login, password and staff proof
delivery on healthy/closing/actual-backpressure transports; three direct staff
boundary cases cover the same transport guards. Tests verify role and staff-audit
preservation, discarded job cleanup and normal successful behavior.

GCC optimized strict C99: **961 game/runtime + 120 account/security checks and
both TCP suites passed** after this increment. The first backpressure fixture
incorrectly assumed a large message alone would fail; normal flushing correctly
handled it. The corrected fixture fills a nonblocking socket send buffer first.

An initial invocation overriding `CFLAGS` without `-std=c99` also failed at the
legacy `bool` typedef under GCC's modern default dialect. Supplying the documented
language flag allowed the actual regression to run; a separate build fix follows.

Further increments and final matrix results pending.

## Limits

No active hardcoded backdoor was identified in the reviewed source. This is not
proof of absence across unreviewed binaries, Git history, dependencies or deployed
systems. Previous save/disk-full/death-file cleanup and cross-record crash-recovery
limits remain; native character ABI and gameplay are not changed by this work.
No merge, deployment or branch-protection change is authorized by this audit.

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

### Increment 2: disabled owner cleanup

Reproduced: `FAIL: local operator can demote an already-disabled owner`.
The local role-change predicate counted active owners correctly but also blocked
demoting a disabled owner when only one other enabled owner remained. It now
permits that already-disabled target, matching the remote staff API's existing
last-active-owner rule. It does not permit removing the last enabled owner.

Nine account checks cover the disabled session, both attempts to remove the last
active owner, audited demotion, retained disabled state and safe re-enablement with
fresh player-only credentials. GCC optimized strict C99: **961 + 129 checks and
both TCP suites passed**.

### Increment 3: default-credential hygiene and privileged names

Replace the non-account dummy hash's fixed input with 32 random bytes generated
by libsodium for each database opening. Wipe that temporary material whether
hashing succeeds or fails. Stored player hashes, password rules, Argon2id cost,
schema and login/session checks are unchanged.

Thirty additional account checks register five privileged-looking names (including
`Admin`, `Owner`, `Root` and the historical author's handle), verify ordinary player
roles, reject wrong passwords and direct staff API attempts, and check account
behavior across regeneration/restart. An initial assertion incorrectly expected
the earlier fixture's target to remain administrator; that fixture had legitimately
demoted it to builder. The assertion was corrected to the established fixture state.

GCC optimized strict C99: **961 + 159 checks and both TCP suites passed**.

### Increment 4: modern compiler flag overrides

The initial `make CFLAGS='-O2 -Werror' test` failed before executing tests because
command-line flags replaced the Makefile's ordinary `CFLAGS += -std=c99`.
GCC's newer default dialect reserves `bool`, conflicting with the legacy typedef.
Use `override CFLAGS += -std=c99` to retain the existing supported dialect rather
than rewrite gameplay types. The shared CI script now intentionally omits the
dialect from its caller-supplied flags, exercising this Makefile guarantee in
every compiler/mode job without changing the workflow or required-check names.

Forced rebuild with the original failing flag override: **961 + 159 checks and
both TCP suites passed**. README counts and the follow-up audit link are updated.

The first full matrix passed both GCC modes, then Clang rejected one new test's
implicit legacy-enum-to-`int` conversion under `-Werror`. The assertion now uses an
explicit comparison with `FALSE`; production behavior is unchanged. The full
matrix is rerun after that correction.

Both bounded analyzers complete without source diagnostics on all four production
translation units (eight invocations). Limits: 35 seconds each; GCC 384 MiB VM,
Clang 512 MiB VM and max-nodes=5000. Clang analysis initially rejected an unused
linker argument injected by the local Nix compiler wrapper; disabling that
command-line-only diagnostic allowed analysis without suppressing source warnings.
These bounded checks are not exhaustive.

### Final local verification

- All four GCC 15.3.0 / Clang 20.1.8 optimized and ASan/UBSan entries pass the
  shared CI script: **961 game/runtime + 159 account/security checks and both
  real-TCP suites per entry**. Leak detection and strict warnings are enabled.
  No sanitizer findings in the exercised paths. This adds 103 game/runtime and
  39 account/security assertions over the baseline.
- An additional real-TCP probe reads the historical shutdown argument into memory
  without printing or copying it into new source. Anonymous and ordinary-player
  attempts using that exact argument are denied. Explicit local promotion to
  administrator then permits normal shutdown without an argument. Server/admin
  processes exit successfully with empty sanitizer stderr. This extra one-off
  probe is separate from the counted shared-script regressions.
- Targeted searches on all eight production C source files find no outbound
  connection calls, process execution/dynamic loading, startup-constructor hooks
  or encoded-payload decoders. No production default-account bootstrap is present.
- Added-line whitespace and retained historical CRLF style are checked. All
  source additions are explicit; no binary, cache or test-data file is published.
- Hosted verification is maintained in PR #6's body, tied to its current head,
  rather than creating a new untested commit just to insert its own run URL here.

## Limits

No active hardcoded backdoor was identified in the reviewed source. This is not
proof of absence across unreviewed binaries, Git history, dependencies or deployed
systems. Storage-specific limits at this audit's baseline are superseded by
[STORAGE_RECOVERY.md](STORAGE_RECOVERY.md): checked saves, journaled settlement
and injected-error/restart tests are now implemented. Physical power-loss and
production-load limits remain. Native character ABI and gameplay are unchanged.
No merge, deployment or branch-protection change is authorized by this audit.

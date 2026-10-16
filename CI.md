# Native GitHub CI

## Plan and scope

Add automatic checks to the tested `master` baseline
`2826ac8f4572219176220a5b573e587b7e34a5b3`, without modifying game code,
save formats, dependencies at runtime, deployment or branch protection:

1. Add a compiler/mode matrix for GCC and Clang, optimized and ASan/UBSan.
2. Use a single local/CI entry point to build production binaries and run the
   native, account and real-TCP tests. Fail on warnings or test failures.
3. Validate workflow syntax and shell scripts, run all four entries locally,
   then publish a draft PR and inspect the actual GitHub run results.
4. Record failures, hosted-run blockers and remaining limitations below.

## What runs

`.github/workflows/native-ci.yml` checks pull requests targeting `master`,
pushes to `master`, and manual dispatches after the workflow is on the default
branch. All four entries run even if another entry fails. At most two run in
parallel, with a 15-minute timeout per job; newer runs for the same ref cancel
older ones.

Each job uses GitHub-hosted `ubuntu-24.04`, installs the distro's supported
build tools, libsodium/SQLite development headers, Python and OpenSSL, and prints tool
and library versions. Distro versions may differ from local versions and
receive security updates; this is compatibility coverage, not a locked build
environment. No shared build/dependency cache is used.

The shared entry point is `.github/scripts/check-native.sh`:

```sh
CC=gcc BUILD_MODE=optimized bash .github/scripts/check-native.sh
CC=gcc BUILD_MODE=sanitizers bash .github/scripts/check-native.sh
CC=clang BUILD_MODE=optimized bash .github/scripts/check-native.sh
CC=clang BUILD_MODE=sanitizers bash .github/scripts/check-native.sh
```

It validates the compiler/mode before cleaning generated test/build outputs,
uses strict warnings with `-Werror`, builds `glad` and `glad-admin`, and runs
`make check` serially. Sanitizer builds enable address/undefined-behavior
checks, leak detection and fail-fast undefined-behavior reporting. Tests use
disposable fixtures; TCP suites use loopback with ephemeral server ports.

Current release-candidate coverage is 1,111 game/runtime, 159 account/security,
479 storage/recovery and 19 source-artifact regressions, plus five TCP/TLS suites.
The latter include trusted PROXY admission and a certificate-verified TLS
forwarding fixture; OpenSSL only generates disposable test certificates.
See `HOSTING.md` and `RELEASE.md`. Actual Mudlet/HAProxy deployment remains separate.

## Security boundaries

- The only third-party action is `actions/checkout`, pinned to the full commit
  for upstream release v7.0.1. Checkout does not persist authentication.
- Workflow token permissions are limited to `contents: read`. There are no
  secret inputs, deployments, package publication or write-capable PR triggers.
- No `pull_request_target`, self-hosted runner, player-data artifact upload,
  host security-setting changes or automatic merge is configured.
- CI does not make checks mandatory for merging. Adding a protected-branch
  requirement is a separate consequential repository setting.

## Verification and failures

The following counts describe the initial CI introduction, not current coverage.
Current-head hosted verification is recorded in PR #6 after the run completes.

- `actionlint` 1.7.7, ShellCheck 0.10.0 and `bash -n` pass. Three invalid
  compiler/mode cases exit with status 2 before running build/cleanup commands.
- All four shared-script entries pass locally: **458 game/runtime + 120
  account/security checks and both real-TCP suites per entry**. Toolchain:
  GCC 15.3.0, Clang 20.1.8, Python 3.13.11, libsodium 1.0.20, SQLite 3.48.0.
  Sanitizer runs report no findings in exercised paths.
- Workflow/script implementation required no test-failure fix during local
  validation. No game, account, native save or test-source file was modified.
- Hosted execution is tracked in the PR's Checks/Actions results and summarized
  in the PR description after execution. Local success does not by itself
  establish Ubuntu runner compatibility or GitHub execution success.

## Remaining limits

Only Linux x86-64 is covered automatically. The ABI-dependent native saves,
TLS requirements and load/fuzz/crash/platform limits documented in
SECURITY_FIXES.md and FOLLOWUP_FIXES.md still apply. Bounded static analyzers
were run during the audit but are not part of this four-job runtime matrix.
GitHub Actions availability, runner provisioning and package mirrors can
fail independently of the MUD tests.

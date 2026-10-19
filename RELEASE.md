# Public source release record and operations

## Current publication status

The public `v1.0.0-rc.1` prerelease was published on 2026-10-16 from merged
`master` commit `cb592315b102da426f0daceae379cde85e149d4a`. The tag resolves
directly to that commit. This is a source release, not a deployed game server.
GitHub provides generated source downloads for the tag; the release has no
separately attached archive, signature, or checksum asset.

PR #6 records the final hosted checks and a verified 47-file source-archive
rehearsal/rebuild for the merged commit. The pre-publication checklist below is
historical, not a pending release gate. Actual deployment/client checks and any
separately retained or signed source archive remain distinct follow-up work.

## Publication plan (recorded before release)

Before publication, `v1.0.0-rc.1` was selected as the public-source candidate.
The plan was to finish the public README, verify the final PR checks, merge PR #6,
then publish the prerelease from the exact merged `master` commit—not the
unmerged PR head. That sequence is complete; see the publication status above.

Earlier audit/rehearsal observations below are historical, not a statement that
the repository is private or that the approved version is still undecided.
No public game endpoint is deployed by the source-release process. Derivative
development remains separate.

## Scope and audit

Release the maintained legacy game first, starting with fresh characters. Keep
the derivative separate after the approved baseline. No real-character migration
or seeded player database is required. Do not reset/delete an operator's existing
directory: "fresh" means a newly created, separate private data directory.

At audit start PR #6 head was
`f7fa23a3d081f9987630ed75162184e52a1bff05`: 36 regular text/source/document files,
no tracked binary, database, cache or player records, and no existing GitHub
releases. The local development checkout contains generated binaries/test folders
and obsolete unpublished artifacts; it is **not** a release payload and they were
not executed or deleted by this pass. Build archives from a pinned clean Git tree,
never `tar`/zip the development working directory.

Licensing provenance and dependency notices are in `THIRD_PARTY_NOTICES.md`.
`LICENSE`, `glad.txt` and original notices are retained. README's old license
inference and upstream AppVeyor badge are superseded; this is not a relicense.
The historical manual includes an obsolete shutdown argument: no current game
command uses it, no value is reproduced here, and old authentication guidance
must not be revived.

## Fresh build and account bootstrap

On Ubuntu/Debian, install build/test prerequisites:

```sh
sudo apt-get update
sudo apt-get install build-essential clang pkg-config libsodium-dev libsqlite3-dev python3 git openssl
```

Validated runtime requirements are C99/POSIX, libsodium 1.0.18+, SQLite 3.31+
and threads. Test/release tooling needs Python 3.10+. Linux x86_64 is the verified
target, not a promise of native Windows/macOS/32-bit support.

In the freshly cloned/unpacked source checkout, **before building**:

```sh
make release-check
make CC=gcc CFLAGS="-O2 -Werror" all check
```

Run from an entirely new private directory, as a dedicated non-root OS user:

```sh
install -d -m 700 "$HOME/gladiatorpits-data"
cd "$HOME/gladiatorpits-data"
/absolute/path/to/source/glad
```

Never put the executable, source checkout or data directory under a web document
root. The server listens on loopback port 3000 unless deliberately overridden.
For local bootstrap, connect through loopback or a secure tunnel. In the MUD:

```text
register YourAccount
```

Answer separate password/confirmation prompts. Never place credentials in shell
arguments, scripts, command history, issue reports or a source archive. In another
local terminal using the **same data directory and OS user**:

```sh
cd "$HOME/gladiatorpits-data"
/absolute/path/to/source/glad-admin role YourAccount owner
```

Back in the MUD, `account` should show Owner, `staff` should show the permitted
menu, and `create YourCharacter` begins ordinary character creation/training.
There is no automatic first-user Owner or default password. `shutdown` is an
authorized Owner/Administrator command with no shared argument.

## Encryption and public-hosting gates

This is a source release, not a public-hosting certification. Keep the backend
on loopback/private networking and require a trusted TLS gateway or secure tunnel
before any remote account password. Never suggest disabling certificate validation.
Test actual certificates, hostname verification, reconnects and password echo in
Mudlet before advertising a public endpoint. Mudlet testing is currently deferred.

Ordinary TCP TLS proxies collapse clients into one per-IP login bucket. Optional
strict `GLAD_TRUSTED_PROXY` mode now accepts bounded PROXY v1 from one explicitly
trusted transport address, forwarding full IPv4/IPv6 client identity while
preserving the five-per-IP, sixty-global and eight-job limits. It rejects
untrusted peers and missing/invalid headers instead of silently falling back.
See `HOSTING.md` and `examples/haproxy.cfg`. The synthetic certificate-verified TLS
fixture passes; real gateway, certificate/firewall and Mudlet deployment checks
still apply. A bind override alone does not enable encryption or proxy identity.

## Source artifact verification and preparation

`RELEASE_FILES.txt` is the explicit sorted payload. The read-only checker rejects
unexpected/missing files, binaries/NULs, databases, caches, common private-key/token
formats, missing retained notices, traversal, links, duplicate archive members,
oversized members and group/world-writable archive files. No matched secret values
are printed. Pattern scans do not certify all secrets absent or audit Git history.

`make release-check` tolerates only checkout `.git` metadata; run it on a fresh
tree. It intentionally rejects the development working directory's build outputs
and unlisted artifacts rather than deleting them. Strict archive checks never
tolerate `.git`. An attacker who controls the manifest/checker can bypass them:
these are trusted-maintainer safeguards, not a signature or external attestation.

For a future, separately reviewed source-archive attachment, substitute an
explicit reviewed full commit for COMMIT:

```sh
git -c tar.umask=0077 archive --format=tar --prefix=gladiatorpits/ COMMIT > /tmp/gladiatorpits-source.tar
python3 scripts/release_check.py --archive /tmp/gladiatorpits-source.tar
sha256sum /tmp/gladiatorpits-source.tar
```

Run from a clean checkout containing this checker/manifest. Keep output outside
the checkout. The explicit archive umask is required for the secure file-mode
policy; do not assume platform/Git archive defaults. The checker never extracts
archives. Validate before extracting/building in a separate staging directory.
For any separate archive attachment, publish its digest only after validating
the archive against the approved commit. At the time of the original audit
rehearsal, no source archive, release, or tag had yet been published.

## Publication record and follow-ups

The pre-publication approval sequence is complete for `v1.0.0-rc.1`: PRs #1–#6
were merged, final hosted checks passed, and the tag/release were published from
the merged commit identified above. This does not claim a separate signed archive
or checksum asset; GitHub's generated tag archives are the available downloads.

The repository does not record an off-GitHub retained archive or digest. If one
is wanted for support or distribution, generate, validate, rebuild, and retain it
separately with its full commit and SHA-256. Derivative development remains a
separate project based on the approved release baseline.

Before hosting a public game, still complete the encrypted-transport, peer-IP /
rate-limit, real Mudlet/client and deployment checks above. No public game server
deployment is implied by publishing source.

One-hour endurance is deferred by choice, not falsely marked complete. The
existing two primary five-minute soaks plus memory diagnostic are in `SOAK_TEST.md`.

## Backup, failure and rollback

For a fresh public install, never package account DBs, credentials or characters.
Once it has users, stop the server or take a coordinated snapshot of character
files, accounts DB/WAL metadata and pending settlement journal together. SQLite
main-file-only copies while WAL is active are unsafe.

On storage failure, retain pending intent and stop accepting players; repair the
cause and restart a compatible build to replay. Do not delete a journal/lock to
force startup, or roll back to a build that ignores pending intent. Finish recovery
and shut down cleanly first, or restore a coordinated backup into a separate
private directory. Verify ownership, account login and counters before switching
traffic. No automatic downgrade, destructive reset, merge, tag or deployment is
performed by this document. `STORAGE_RECOVERY.md` is the detailed operator guide.

## Verification evidence

The fresh-artifact rehearsal and final hosted verification below are recorded
in PR #6 and tied to the published head. The release version is `v1.0.0-rc.1`;
publication does not substitute for the operational checks listed above.

- A private 47-file source snapshot was checked, archived with Git's explicit
  private umask, validated without extraction, unpacked, checked again and built.
  GCC 15.3.0/Clang 20.1.8 optimized and ASan/UBSan profiles all passed: 1,111
  game/runtime + 159 account/security + 479 storage/recovery + 19 release checks,
  and all five TCP/TLS suites per profile. No sanitizer/leak findings appeared
  in those exercised paths.
- After isolating inherited operator proxy settings in the direct-mode fixtures,
  the full GCC sanitizer suite passed again with deliberately invalid
  `GLAD_TRUSTED_PROXY` inherited by the test runner.
- Clang's analyzer and GCC `-fanalyzer` passed for the game translation unit,
  including the new proxy parser. `actionlint`, ShellCheck and `bash -n` passed.
  Original MIT permission text remains unchanged except the additional requested
  contribution copyright line; original author/source notices remain.
- Hosted current-head results are recorded in PR #6 rather than an endlessly
  changing self-referential release commit. The rehearsal did not create a signed
  archive asset or approve hosting; the published tag/release is verified above.

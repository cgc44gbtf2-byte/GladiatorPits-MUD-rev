# Gladiator Pits MUD

[![Native CI](https://github.com/cgc44gbtf2-byte/GladiatorPits-MUD-rev/actions/workflows/native-ci.yml/badge.svg)](https://github.com/cgc44gbtf2-byte/GladiatorPits-MUD-rev/actions/workflows/native-ci.yml)

**v1.0.0-rc.1 — public source release candidate**

A modernized version of Richard Woolcock's original C player-versus-player arena
MUD, maintained with modernization contributions by LaWaylon Battles (aKa Yami).
This repository is public. Runtime accounts, passwords, character records and
operator data must remain private and must never be committed or distributed.

This release preserves the legacy game rather than rewriting it. Derivative
development will use a separate baseline/branch after this release; those future
features are not part of this candidate.

### What is included

- Legacy training/stat formulas, challenges, arena combat, surrender, permanent
  death, titles and progression; original native player-record layout retained.
- Prompted accounts with Argon2id password hashes, character ownership, password
  changes, session revocation and Player/Builder/Moderator/Administrator/Owner roles.
- An in-game staff dashboard for inspection, account access, roles and auditing.
- Checked saves, recoverable combat settlement, startup replay and storage locking.
- Optional trusted PROXY v1 support for TLS gateways, including IPv4/IPv6 client
  identity and separate per-player-address login limits.
- GCC/Clang CI, native regressions, real TCP/TLS tests and source-package checks.

This is a release candidate, not a promise of production readiness or a complete
world editor. Read [release notes](RELEASE_NOTES.md), [hosting guidance](HOSTING.md),
[release/recovery procedures](RELEASE.md) and [attribution](THIRD_PARTY_NOTICES.md).

### Download and build

Use the versioned source from the
[GitHub releases page](https://github.com/cgc44gbtf2-byte/GladiatorPits-MUD-rev/releases)
or clone the release tag:

```sh
git clone --branch v1.0.0-rc.1 --depth 1 https://github.com/cgc44gbtf2-byte/GladiatorPits-MUD-rev.git
cd GladiatorPits-MUD-rev
```

Build on a POSIX system with a C99 compiler, Make, pkg-config, libsodium
development headers (1.0.18+), and SQLite development headers (3.31+). This
build produces the game and its local account administration tool:

```sh
# Ubuntu/Debian; Python/OpenSSL are for tests, not the running MUD.
sudo apt-get update
sudo apt-get install build-essential pkg-config libsodium-dev libsqlite3-dev python3 openssl git
```

```sh
make release-check  # before building, in the fresh clone/unpacked source
make
```

Linux x86_64 is the validated target. Other POSIX systems may require changes;
native Windows, macOS and other architectures are not verified support claims.
The modernization history and compatibility boundaries are in
[MODERNIZATION.md](MODERNIZATION.md).

The default TCP port remains 3000. A different port can be selected at build time, for example with `make CPPFLAGS=-DPORT=4000`.

### Running and player data

Run the executable from a private data directory (prefer mode 0700): player records,
`log.txt`, and the new `accounts.sqlite3` database are stored in its working
directory. The directory must belong to the running OS user and not be writable
by group/other users; the database must be a regular, owner-only file. Back up
existing player files before testing any new build. A fresh public installation
starts in a new data directory with no accounts or characters; never delete or
reset another installation to achieve that. Native records are not migrated.

From the built source directory:

```sh
SOURCE_DIR="$(pwd)"
install -d -m 700 "$HOME/gladiatorpits-data"
cd "$HOME/gladiatorpits-data"
"$SOURCE_DIR/glad"
```

The server now binds **127.0.0.1** by default. Put it behind a TLS-capable MUD
gateway or secure tunnel before transmitting passwords remotely. An explicit
IPv4 `GLAD_BIND_ADDRESS` can override the interface for a protected deployment;
it does not enable TLS by itself.

For gateways that send PROXY v1, optional `GLAD_TRUSTED_PROXY` preserves full
IPv4/IPv6 client identity without sharing every player's login-limit bucket.
This strictly trusts one actual gateway address and requires a valid header
before any application traffic; it is disabled by default. Read
[HOSTING.md](HOSTING.md) for setup, threat boundaries, tests and an HAProxy example.

Telnet negotiation and subnegotiation frames are consumed separately from game
commands/passwords, including across fragmented reads. Unsupported options are
refused; terminal-size payloads are ignored rather than interpreted as input.
This framing support is not TLS or a complete terminal/line-editing implementation.

Use `register <account>` or `login <account>` and answer the separate password
prompts, then `create <character>` as before. Passwords are 15-128 printable ASCII
characters, stored only as Argon2id hashes. `password` changes your password and
revokes other sessions; `account` displays your account and current staff role.
Never type a password as a command argument.

New accounts are always Player. Staff roles and legacy character ownership
require an explicit local operator action using `glad-admin`. An existing
unlinked character cannot be claimed merely by knowing its name. Accounts may
own multiple characters; permadeath retains name ownership, not dead progression.

The `shutdown` command now requires Administrator or Owner permission and no
shared password. `GLAD_SHUTDOWN_PASSWORD` is no longer used. The historical
`glad.txt` is unchanged; its passwordless loading and shutdown instructions are
superseded by this README and [ACCOUNTS.md](ACCOUNTS.md).

### Staff dashboard

The dashboard is **inside the MUD**, using the same account and connection.
After logging in with a staff account, type `staff` or `dashboard`. The menu is
filtered by your current permissions; regular Players cannot see or use it.

```text
staff
staff world
staff accounts
staff characters
staff online
staff inspect Character
staff disable Account
staff enable Account
staff role Account moderator
staff audit
```

Owners manage roles/staff access and audit history. Moderators/Administrators
manage regular Player access and inspect characters. Builders see the overview
and world inventory. Changes require `CONFIRM` and a separate current-password
prompt—never put a password in a command. Character inspection is read-only;
world/player editing is not implemented. There is no browser console, extra
listening port, web dependency or web session system.

Bootstrap Owner locally as described in [ACCOUNTS.md](ACCOUNTS.md); no default
staff credentials exist. Register your account through the separate MUD password
prompts, then, in another local terminal, run the administration tool from the
same private data directory:

```sh
cd "$HOME/gladiatorpits-data"
/absolute/path/to/GladiatorPits-MUD-rev/glad-admin role YourAccount owner
```

Verify `account` and `staff` in the MUD. New registrations are always Player;
the first registrant is not automatically staff. Read [DASHBOARD.md](DASHBOARD.md) before upgrading:
account schema 1 → 2 is additive, but back up/stage data and upgrade the game and
local admin tool together. Native player files are not migrated.

See [SECURITY_FIXES.md](SECURITY_FIXES.md) for the subsequent request-cancellation,
deadline, progression-overflow and Telnet fixes, their tests and remaining limits.
The post-merge [follow-up log](FOLLOWUP_FIXES.md) records input-time expiry and
bounded, collision-checked arena-ID allocation. Ordinary combat rules and native
save fields are unchanged.
Native win/loss/kill counters retain their existing `int` layout; at `INT_MAX`
they retain that value and log the ceiling instead of wrapping negative.

### Tests

Runtime dependencies are libsodium, SQLite, and POSIX threads. Test/release tools
use Python 3.10+ standard libraries; the TLS fixture also uses the OpenSSL command
to generate a disposable certificate (not a MUD runtime dependency).

```sh
make release-check # source payload check BEFORE building, in a fresh checkout
make check       # 1111 game + 159 account + 479 storage + 19 release; five TCP/TLS suites
make soak-test   # optional five-minute, 32-client multiplayer run; temporary data only
make sanitize    # repeat all native/TCP suites under ASan/UBSan
make clean
```

The [GitHub CI guide](CI.md) documents automatic GCC/Clang optimized and
ASan/UBSan checks, local reproduction, permissions and hosted-run results.
The [combat/security audit](COMBAT_SECURITY_AUDIT.md) records disconnect safety,
secure D10 rolls, shutdown ownership cleanup, monotonic combat timing and
current-session challenge admission, including regressions, compatibility
boundaries and verification.
The [backdoor/security follow-up](BACKDOOR_SECURITY_AUDIT.md) records the historical
shutdown credential review, failed-output authorization guards, safe disabled-owner
cleanup, privileged-name tests and randomized timing-only dummy hash.
The Makefile preserves the required C99 dialect when overriding `CFLAGS`.
The [storage recovery guide](STORAGE_RECOVERY.md) describes checked saves,
recoverable surrender/permadeath settlement, startup replay, the data-directory
lock and coordinated backups. Storage errors stop gameplay instead of silently
continuing; retain pending intent for recovery rather than deleting it.
The opt-in [multiplayer soak](SOAK_TEST.md) checks simultaneous duels, disconnect
settlement, connection/authentication pressure, sampled resources and restart
ownership/progress. It is bounded validation, not a production capacity claim.

Tests use disposable directories and an ephemeral TCP port, not live player records. GCC 15 and Clang 20 have been verified; use `make clean` before switching compiler or build flags.

### Release-candidate limitations

Public source availability does not mean a live public server is deployed.
The backend is plain TCP and must stay protected behind a tested encrypted
gateway/tunnel before remote password use. An HAProxy example and automated
certificate-verified forwarding fixture are included, but actual HAProxy/Mudlet
deployment, certificate renewal and production firewall/routing are not certified.

The staff interface is in-game; character inspection is read-only and full
world/player editing is future work. Native saves/journals depend on the ABI and
endianness of the build. Five-minute synthetic soaks are not long-duration or
capacity certification; the longer endurance run remains deferred. No test suite
proves the code leak-free or vulnerability-free.

### History

Gladiator Pits is a [MUD](https://en.wikipedia.org/wiki/MUD), written by Richard Woolcock, who wrote the now-famous [GodWars](https://en.wikipedia.org/wiki/GodWars) codebase.

MUDs are text-based multiplayer games. Gladiator Pits is a compact PvP arena game
whose original source and documentation are preserved here.

The [original C upstream](https://github.com/servusdei2018/GladiatorPits-MUD/tree/original)
and the separate [upstream Go rewrite](https://github.com/servusDei2018/GladiatorPits-Go)
are historical references, not dependencies of this maintained C release.

### License

Richard Woolcock wrote in `glad.txt`, <blockquote>This code is copyrighted to me, however you're welcome to use it for anything 
you wish, so long as you don't remove the copyright notices from any source 
files containing any of the mud source code.</blockquote>

The repository retains the MIT text inherited from upstream in [LICENSE](LICENSE),
alongside the original permission above. Preserve both that file and original
source notices when redistributing or deriving from this code. This modernization
does not relicense the original work. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
for checked provenance and dependency attribution.

Modernization contributions: Copyright (c) 2026 LaWaylon Battles (aKa Yami).
Richard Woolcock's original game copyright and permission notices remain intact.

### Disclaimer

Richard Woolcock is credited for the original game; LaWaylon Battles (aKa Yami)
is credited for modernization contributions, not authorship of the original.
The public source is provided under the retained license and permission notices,
without warranty. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

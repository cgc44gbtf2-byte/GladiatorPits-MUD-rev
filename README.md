# Gladiator Pits

[![Build status](https://ci.appveyor.com/api/projects/status/e4l064rgu0fe5hax?svg=true)](https://ci.appveyor.com/project/servusDei2018/gladiatorpits-mud)

(A Go rewrite is available [here](https://github.com/servusDei2018/GladiatorPits-Go).)

### Building

This maintains the [original](https://github.com/servusdei2018/GladiatorPits-MUD/tree/original) C game rather than rewriting it. The modernization pass preserves its stat formulas, training rules, PvP combat, progression, and native binary player record layout while improving I/O safety, persistence, and portability. See [MODERNIZATION.md](MODERNIZATION.md) for the audit, phased plan, change/failure log, and remaining issues.

Build on a POSIX system with a C99 compiler, Make, pkg-config, libsodium
development headers (1.0.18+), and SQLite development headers (3.31+). This
build produces the game and its local account administration tool:

```sh
make
```

The default TCP port remains 3000. A different port can be selected at build time, for example with `make CPPFLAGS=-DPORT=4000`.

### Running and player data

Run the executable from a private data directory (prefer mode 0700): player records,
`log.txt`, and the new `accounts.sqlite3` database are stored in its working
directory. The directory must belong to the running OS user and not be writable
by group/other users; the database must be a regular, owner-only file. Back up
existing player files before testing any new build. Native player records are
not migrated or reset.

The server now binds **127.0.0.1** by default. Put it behind a TLS-capable MUD
gateway or secure tunnel before transmitting passwords remotely. An explicit
IPv4 `GLAD_BIND_ADDRESS` can override the interface for a protected deployment;
it does not enable TLS by itself.

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
staff credentials exist. Read [DASHBOARD.md](DASHBOARD.md) before upgrading:
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

Runtime dependencies are libsodium, SQLite, and POSIX threads. The TCP and
in-game staff integration tests use Python 3's standard library:

```sh
make check       # 458 game + 120 account checks, gameplay and in-game staff TCP suites
make sanitize    # repeat all native/TCP suites under ASan/UBSan
make clean
```

Tests use disposable directories and an ephemeral TCP port, not live player records. GCC 15 and Clang 20 have been verified; use `make clean` before switching compiler or build flags.

### History

Gladiator Pits is a [MUD](https://en.wikipedia.org/wiki/MUD), written by Richard Woolcock, who wrote the now-famous [GodWars](https://en.wikipedia.org/wiki/GodWars) codebase.

Popular in the 20th century, MUDs are text-baased Multi-player games, similar to our modern day MMOs.

Gladiator Pits, of which this source code is the only public version (to my knowledge) still available, is a excellent example of a MUD, clearly commented, with precision and intuitivity.

### License

Richard Woolcock wrote in `glad.txt`, <blockquote>This code is copyrighted to me, however you're welcome to use it for anything 
you wish, so long as you don't remove the copyright notices from any source 
files containing any of the mud source code.</blockquote>

And therefore, the most applicable license appears to be the MIT license.

### Disclaimer

I in no way claim authorship, nor authority, over this excellent piece of code,
but release it, under its author's original terms and conditions, to the general
public.

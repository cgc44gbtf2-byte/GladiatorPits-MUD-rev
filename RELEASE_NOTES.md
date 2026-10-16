# Clean-source modernization candidate — unreleased

This is the maintained legacy C game, not the planned derivative. It starts with
fresh accounts/characters; no existing-player migration is a release requirement.
Do not ship seeded characters, staff accounts, databases or credentials.

## Preserved

The existing PvP arena, training/stat formulas, random roll ranges, damage/speed
rules, challenges, surrender, permanent death, titles and progression remain.
Original source copyright/permission notices and native record field layout are
retained. This is not a rewrite or a claim that future expansion ideas in
`glad.txt` (such as mobiles/extra areas) already exist.

## Modernized

- Strict C99 GCC/Clang builds with libsodium, SQLite and POSIX threads.
- Prompted accounts/password changes, Argon2id hashes, ownership reservation,
  role capabilities, session revocation and an in-game staff dashboard.
- Explicit local operator bootstrap; no default staff credentials or shared
  shutdown password. Original manual authentication/shutdown text is historical.
- Bounded I/O/Telnet framing, secure D10 randomness, monotonic combat timing,
  disconnect/admission guards and checked/capped integer progression.
- Checked native saves and recoverable winner/loser/permadeath settlement,
  process lock, startup replay and fail-closed storage diagnostics.
- Native, account, recovery, real-TCP and optional multiplayer soak tests.
- Source-release manifest/checker and isolated negative artifact fixtures.
- Optional strict trusted PROXY v1 admission, full IPv4/IPv6 login-limit keys,
  TCP/TLS forwarding tests and an illustrative HAProxy gateway configuration.
- Copyright for modernization contributions: LaWaylon Battles (aKa Yami), 2026,
  including source/header files; original Richard Woolcock notices retained.

## Interfaces and support

The default bind address is 127.0.0.1, default port 3000. Public passwords require
tested encrypted transport; a bind override is not TLS. See `RELEASE.md` for the
TLS-proxy shared-IP/rate-limit caveat. Mudlet is the primary intended client, but
actual Mudlet UI/TLS compatibility remains deferred and is not certified here.

Validated targets: Linux x86_64, local GCC 15.3.0/Clang 20.1.8 and Ubuntu 24.04
hosted CI compilers. POSIX interfaces are required; native Windows/macOS/other
architectures are not verified support claims. Python 3.10+ is needed for test and
release tooling (not the running MUD); Python 3.13 was used locally.

## Limits

World/player editing is future work; the dashboard currently inspects and manages
access/roles, not a complete world editor. Native records/journal are ABI/endian
dependent. No native TLS listener, PROXY v2 parser, HTTP interface or automated
certificate management is added. Five-minute synthetic soaks are not one-hour/
24-hour endurance or supported-player capacity guarantees. Physical power loss,
real disk exhaustion, deployed TLS and actual Mudlet sessions remain unverified.
Read `STORAGE_RECOVERY.md` before operating a persistent instance.

The source candidate is not a security/leak-free certification. Merge, version,
tag, source-asset publication and derivative branching require separate approval.

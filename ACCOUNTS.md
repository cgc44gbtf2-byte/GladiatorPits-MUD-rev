# Account and staff-role increment

## Plan (recorded before implementation)

1. Add a separate account store with libsodium Argon2id passwords and SQLite
   transactions. Keep native character records and all gameplay formulas intact.
2. Test registration, password verification/change, ownership, role permissions,
   disabled accounts, session revocation, and restart persistence.
3. Add bounded background authentication work and Telnet-friendly password
   prompts; never accept passwords as command arguments or log them.
4. Require authenticated ownership before creating/loading a character. Reserve
   existing unlinked saves for an explicit local staff linking operation.
5. Gate commands centrally by capabilities, add local administrative tools and
   regression/TCP coverage, then verify GCC/Clang and sanitizers.

## Security boundaries

- New accounts are always Player. There is no first-registrant administrator,
  remote role-grant command, default password, or client-supplied role.
- Roles are Player, Builder, Moderator, Administrator, Owner. Permissions are
  explicit, not comparisons of numeric rank. The in-game staff dashboard implements
  account-access moderation and Owner-only role management; world/player editing
  remain reserved capabilities, not implemented editors. Every handler must
  enforce capabilities server-side even when buttons/commands are hidden.
  See [DASHBOARD.md](DASHBOARD.md) for the separate dashboard increment.
- Account IDs/character bindings are stored separately from raw player bodies.
  An unlinked existing save cannot be claimed over the network. An operator with
  filesystem access verifies ownership and explicitly links it. Death retains
  the character-name binding, not the dead body or progression.
- Password hashing runs outside the game loop on a bounded worker queue. Pending
  jobs identify connections by unique tokens, never dangling connection pointers.
- TCP is not TLS. The account-enabled server must default to loopback and be put
  behind a TLS-capable MUD gateway or secure tunnel before remote password use.
  Telnet echo negotiation only hides local echo; it does not encrypt traffic.
- The database, WAL/SHM files and backups contain sensitive authentication data.
  Keep them private, never commit them, and back them up with character records.

## Verification and known limits

No production deployment or real player migration is part of this draft.

### Implemented interfaces

In the MUD:

```text
register AccountName
login AccountName
account
password
create CharacterName
shutdown
```

Registration/login/password changes prompt separately for credentials. Telnet
WILL/WONT ECHO negotiation suppresses local echo on supporting clients; clients
that ignore it must disable their own echo. Passwords never appear in command
arguments, server responses, logs or audit entries. Consumed input, pending
connection credentials and completed worker-job buffers are explicitly wiped.
Unknown commands no longer reflect their text back, avoiding accidental password
disclosure when a password is pasted outside its prompt.

The game now requires libsodium 1.0.18+, SQLite 3.31+, pkg-config and POSIX threads.
Argon2id uses libsodium's interactive parameters (currently 64 MiB and two
iterations), a fresh random salt per password and library-provided verification.
SQLite stores account IDs, salted hashes, role, disabled state, session epoch,
character bindings and security audit metadata using bound statements,
transactions, foreign keys, WAL and FULL synchronization. Database files are
owner-only and symlink paths are rejected.

The game defaults to IPv4 loopback. `GLAD_BIND_ADDRESS` is an explicit interface
override, not TLS. Protect the game with a TLS MUD gateway or SSH/private tunnel
before remote use. No public service was configured or deployed.

### Staff capability model

| Role | Play | Staff dashboard | World edit | Player moderation | Player edit | Shutdown | Manage roles |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Player | Yes | No | No | No | No | No | No |
| Builder | Yes | Yes | Yes | No | No | No | No |
| Moderator | Yes | Yes | No | Yes | No | No | No |
| Administrator | Yes | Yes | Yes | Yes | Yes | Yes | No |
| Owner | Yes | Yes | Yes | Yes | Yes | Yes | Yes |

Existing gameplay commands retain their rules. The in-game `staff`/`dashboard`
handlers implement staff views, Player access moderation and Owner role management.
World/player editing remain authorization primitives for future handlers.
Command visibility and command execution both use the permission
metadata in the command table. Roles are reread from storage, not trusted from
clients or cached indefinitely. An invalid role or unknown capability is denied.
Anonymous spectator WHO/say/emote behavior is retained.

### Local bootstrap and migration

Run `glad-admin` in the same private data directory and OS identity as the game.
It never accepts a password and is **not** a remotely exposed staff command.
First register your account through the protected MUD connection, then grant it
Owner locally. There is no automatic owner selection:

```sh
/path/to/glad-admin role MyAccount owner
/path/to/glad-admin role BuilderAccount builder
/path/to/glad-admin role ModeratorAccount moderator
/path/to/glad-admin role AdminAccount administrator
/path/to/glad-admin link MyAccount Existinghero
/path/to/glad-admin disable AccountName
/path/to/glad-admin enable AccountName
```

Verify the actual player's ownership before linking a legacy save. The link
operation changes only metadata and will not silently transfer an already owned
character. It neither rewrites nor validates/repairs the historical body record;
the normal player loader still validates it. Unknown/incompatible records remain
untouched. The store independently checks canonical character paths, so callers
cannot bypass reservation by changing case or misreporting whether a file exists.

Role changes and local enable/disable operations are audited with the target
account ID, operation/new role or access state, and timestamp. The last enabled
Owner cannot be demoted or disabled. The local tool is a trusted filesystem
operator boundary and may grant roles. The dashboard never exposes those trusted
local functions: its separate network-facing account functions transactionally
require actor permissions. There is currently one role per account.

Password changes increment the account's session epoch. Other logged-in sessions
are rejected, while the changing connection receives the new epoch. Disabling
and re-enabling also invalidates old sessions. Revoked sessions are closed on the
next command or periodic poll; if currently fighting, disconnect follows the
existing surrender rule. Multiple characters/accounts do not share staff status.
Account roles are not stored inside character saves.

### Incremental verification log

- Store and local CLI: compiled with strict warnings; **81 account checks** passed
  normally and under ASan/UBSan while the original **100 gameplay checks** and TCP
  suite still passed before command integration.
- Runtime integration: **123 gameplay/permission checks** passed alongside the
  store suite after fixtures were given explicit authenticated accounts.
- Backpressure/timeouts and credential safety: bounded queue, per-IP throttling,
  password-input wiping, prompt confirmation, and cancelled-flow coverage raised
  the game/runtime suite to **132 checks**.
- Independent legacy-file detection, canonicalization and symlink rejection
  raised the account/security suite to **84 checks**.
- Non-reflecting unknown-command feedback and an out-of-prompt password-paste
  check raised the game/runtime suite to **133 checks**.
- Final matrix: GCC 15.3.0 and Clang 20.1.8, optimized and ASan/UBSan builds,
  strict default warnings plus `-Werror`: **133 + 84 checks and the expanded TCP
  integration suite pass**. The TCP suite verifies gameplay, wrong passwords,
  character theft/legacy reservation, local linking, restart persistence, role
  changes, password revocation (including the current-session race), connection
  replacement, rate limits and staff-only shutdown. Test passwords and all
  player/account records are disposable fixtures.

The tested libraries were libsodium 1.0.20 and SQLite 3.48.0. They are linked as
system dependencies, not vendored or replaced by custom cryptography.

Failures encountered and resolved:

- A third combat fixture initially lacked its new authenticated session; its
  command was correctly denied. The fixture was fixed, not the gameplay rule.
- A bounded-queue fixture initially wasn't linked into the game's output-routing
  list, so its expected message was missing. The fixture was linked correctly.
- The first TCP password-change test timed out because C consumed the first
  letter of "Current" into an adjacent hexadecimal Telnet escape. Separating the
  string literals fixed the actual prompt, then all matrices passed.
- During implementation review, the password-change result and periodic
  revocation checks could race to close the changing connection. In-flight
  authentication sessions now await their result/timeout before epoch validation;
  completed results are revalidated before granting access.
- A whole-file whitespace check flagged pre-existing trailing spaces in the
  README's historical license quotation. Those lines were preserved; the
  incremental diff whitespace check passes with the existing CRLF convention.

### Remaining limits and deployment checklist

- Back up character files and account storage together. Use SQLite's online
  backup facility or stop the server before copying the database; copying only
  the main DB while WAL is active is not a consistent backup.
- Protect filesystem access and database backups. Losing ownership metadata must
  not trigger public re-claiming of legacy characters. No account reset or
  destructive migration is performed by startup.
- There is no email verification, self-service password recovery, MFA,
  character transfer or player/world editing. The new staff dashboard uses
  authenticated, permission-checked functions rather than exposing internal local
  administration functions. It uses the existing MUD account/connection session,
  explicit confirmation and a separate current-password prompt. No web server,
  browser dashboard, web session or bearer-token API is added.
- Authentication is capped at eight outstanding jobs on one worker, five
  submissions/IP/minute and sixty globally/minute; login/registration/password
  change share those budgets. IP budgets use a bounded 64-entry table and reset
  on process restart; this is not a distributed anti-abuse system. Shared NAT
  clients may need production-specific limits. Password flows expire after
  120 seconds. No expensive password hash runs in the game loop.
  A plain TCP TLS proxy commonly makes all connections appear to originate from
  its loopback IP, sharing that five-per-minute budget. Real-IP preservation or a
  reviewed gateway/limit adaptation is required for higher-volume deployment;
  this server does not parse or trust arbitrary forwarded-IP headers.
- Credential-store failures deny access. Disk-full/fault-injection, high-load
  stress, third-party security review, and real legacy-player migration have not
  been executed. TLS gateway/client compatibility must be tested before release.
- Native save ABI, original progression and combat/permadeath behavior remain
  unchanged. No live player data, production configuration or `master` branch was
  modified. Original copyright notices, LICENSE and `glad.txt` remain intact.

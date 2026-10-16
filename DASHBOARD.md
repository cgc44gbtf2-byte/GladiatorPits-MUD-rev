# In-game staff dashboard

## Corrected plan, recorded before in-game implementation

The user clarified that “dashboard” means an interface inside the MUD, not a
browser application. The unpublished browser implementation was discarded;
none of it is included in this increment or deployed. Keep the reusable native
authorization, auditing and read-only record safety improvements.

1. Add `staff` and `dashboard` commands to the existing game command dispatcher.
   Show a text menu filtered by the current account's explicit capabilities.
2. Add bounded account/ownership/audit pages, connected-player inspection and
   read-only live/saved character views. Describe the actual world systems;
   do not invent an area editor.
3. Add Owner-only role management and Moderator/Administrator regular-Player
   access management. Require an explicit confirmation and a separate password
   prompt. Reuse the existing bounded authentication worker; never hash passwords
   in the game loop or accept them in command arguments.
4. Recheck actor/target authority transactionally at commit time. A disconnected,
   cancelled, expired or revoked request must not apply a staff mutation.
5. Run native/gameplay and real-TCP tests for each increment, then the GCC/Clang
   warning/sanitizer matrix. Publish a separate draft PR, not a deployment.

## Scope

- Player: no staff command visibility or access.
- Builder: staff overview and world-system inventory.
- Moderator/Administrator: additionally inspect accounts/characters/online
  players and enable/disable regular Players.
- Owner: additionally change roles/staff access and read staff change history.
- No self-role/access changes, no last-active-Owner lockout, no silent legacy
  claiming, no raw player/world editing and no changes to combat/progression.
- All existing gameplay commands and native player record fields remain intact.

## Commands and safeguards

`staff` and `dashboard` show the same text menu in the existing connection.
Accounts/audit pages show at most ten rows, with `staff accounts <after-id>` or
`staff audit <before-id>` for continuation. `staff characters <after-name>` pages
ownership metadata. `staff online` shows the first ten authenticated connections
without IP addresses or credentials. `staff world` lists Stadium 0, Training -1
and dynamically allocated arenas, with the current fighting-connection count.
It is an inventory, not a pretend world editor.

`staff inspect Character` verifies the staff capability and ownership binding,
then displays an authoritative live snapshot if that character is connected, or
the last compatible native save otherwise. It never writes stats, progression,
body bytes or ownership. A missing record can mean unfinished training or death;
an incompatible/unreadable record is retained, not repaired or overwritten.

`staff role Account Role` is Owner-only. `staff disable Account` and `staff enable
Account` allow Moderators/Administrators to manage regular Players; only Owners
may manage staff access. The command summarizes the consequence, then requires
the exact word `CONFIRM`, then separately prompts for the acting account's
current password with Telnet echo suppression. Any other confirmation input
cancels. No-op/self changes are rejected. Do not put passwords in arguments.

Password proof shares the existing bounded worker queue and authentication
budgets. No Argon2 work runs in the game loop. The worker verifies identity/epoch
but cannot apply an action. Only the game thread, after finding the matching
connection token and unique request token and checking deadline/socket liveness, may call the
transactional permission-checked staff functions. Unknown/disconnected results
are discarded; revoked actor/target permissions are rechecked at commit time.
Cancellation, failure and timeout wipe pending credentials/action metadata.
Cancellation also invalidates the expected request token, so a completed proof
from an old action cannot apply during a newer action on the same connection.
All completed authentication responses check their deadline before acceptance.
As with any network action, disconnect cannot undo a transaction already begun.

Access changes revoke sessions; an affected fighter's disconnect follows the
original surrender behavior. The confirmation explicitly warns about this.
Changes append actor/target IDs, before/after values, action and UTC timestamp
to `staff_audit`, plus an event in the existing account `audit`. `staff audit`
shows successful in-game changes only; local operator/authentication history is
still in the separate original audit table. Neither contains passwords/hashes.

## Storage, build and operation

There are no new runtime dependencies beyond the account increment. Build with
the existing C99/POSIX toolchain, libsodium, SQLite, pkg-config and pthreads:

```sh
make clean all check
make sanitize
```

Run the MUD normally from its private data directory, log into a staff account
and type `staff`. No new service, port, browser, Python web framework or web
session system is involved. The existing TCP connection still requires a TLS
MUD gateway/private tunnel before transmitting credentials remotely; Telnet
echo negotiation is not encryption.

The account upgrade is additive schema 1 → 2: create `staff_audit` and an index
on character account ownership, preserving all existing account/ownership/audit
rows. Native body fields, alignment and validation remain unchanged; native
files remain ABI-dependent. Back up a consistent complete private data directory
and stage against a copy first. Stop writers before copying, or use a reviewed
SQLite online-backup/snapshot procedure; copying only the main DB during WAL
writes is not consistent.

Upgrade the game and local `glad-admin` binary together. Older account binaries
reject schema 2 on startup. Do not reset `user_version` or delete tables to
downgrade: use the consistent pre-upgrade backup and matching binaries. No
automatic staff bootstrap or real player migration is performed.

## Incremental verification and failure log

- Before changing the interface, the reusable native record/account changes
  passed 140 game and 117 account checks, gameplay TCP and the browser prototype
  matrix. The browser prototype is not part of this PR and is not evidence for
  the in-game interface.
- The first in-game native pass passed 175 game/runtime checks and 117 account
  checks, plus the original gameplay TCP suite. Added coverage includes menus,
  denied direct commands, confirmation/cancellation, queued main-thread apply,
  expired results, stale connection tokens and a known-disconnected socket.
- The first in-game TCP test incorrectly searched residual permission-denial
  text for the word “staff” when testing command-list visibility. It now consumes
  the complete prior message. No authorization rule was weakened. The real TCP
  staff suite then passed across four server lifetimes.
- The account suite also checks schema-1 upgrade, preserved sessions/ownership,
  direct role/target permission bypasses, no-op protection and injected
  audit-insertion failure rolling back both the change and history. A disposable
  SQLite trigger simulates that failure; startup never installs it.
- Record tests compare all historical field offsets/size and confirm inspection
  leaves save bytes unchanged; symlinks, wrong-size and invalid records fail closed.
- Final in-game matrix: GCC 15.3.0 and Clang 20.1.8, optimized and
  AddressSanitizer/UndefinedBehaviorSanitizer builds, all strict warnings plus
  `-Werror`: **175 game/runtime + 120 account/security checks and both real-TCP
  suites pass in all four configurations**. The additional target-lookup checks
  include case-insensitive exact lookup, invalid names and Player/Builder denial.
  Incremental whitespace checks pass with the historical CRLF convention.

## Remaining issues and release boundary

The post-dashboard audit found gaps in the original cancellation and deadline
tests. [SECURITY_FIXES.md](SECURITY_FIXES.md) records the fixes, failing regressions
and expanded verification; the matrix above describes the original increment.

- No world/player CRUD editor, persistent area/item/mob schema, character
  transfer, recovery, MFA, fine-grained custom grants or real-player migration.
- Native saves remain architecture-dependent. Linux x86_64 is the tested target;
  other systems and production Telnet/TLS gateway clients remain unverified.
- Password work is bounded/off-loop, and list output is bounded. SQLite queries
  and short audit transactions remain synchronous; disk/fsync latency and
  production-load/DoS behavior have not been benchmarked. No production security
  sign-off, disk-full/crash recovery validation or deployment is claimed.
- Native staff actions share the existing five/IP/minute and sixty/global/minute
  authentication budgets. A plain gateway can collapse all users to one observed
  IP; review gateway limits/real-IP handling before a high-volume deployment.
- This draft stacks on the accounts PR, itself on the modernization PR. Merge
  in order and retarget dependent PRs after their parents merge. This work
  does not merge, deploy, alter live configuration or touch real player data.

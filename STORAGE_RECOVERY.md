# Player storage and settlement recovery

## Audit and incremental plan

Baseline: draft PR #6 at `73d922433c50b93f6e4fc43965e952d76ca687b6`.
Master remains `cd38d0efb33468aceab7cd448bf19e209f3882f1`.

The old single-record Save uses a temporary file, file fsync and rename, but
returns no status and does not sync the containing directory. Training announces
success first. Surrender saves winner and loser separately. Death ignores
`remove()` failure and frees the loser before saving the winner. A failure or
crash can therefore produce partial outcomes or leave a dead character loadable.

Implement and test separately:

1. Add a small recoverable storage backend, preserving native body records.
   Prepare absolute after-images in a private, checksummed journal; sync it before
   changing player files, then apply and sync files/deletion before clearing it.
   Add deterministic I/O-failure tests, with hooks compiled only into test targets.
2. Return Save status, commit training before announcing it, and stop gameplay on
   storage failure. Replay valid pending work before opening the listener.
3. Settle surrender and death as one recoverable outcome before changing live
   counters, unlinking fight state or announcing success.
4. Test interrupted application/restart, duplicate replay, invalid journals and
   exclusive data-directory ownership; run the complete compiler/sanitizer matrix.

No changes to combat formulas, death threshold, progression rules, account
ownership, record layout or existing character files' format. No live player
data, automatic merge, deployment or branch-protection changes.

## Recovery contract

One game process owns the data directory using an advisory lock. Each prepared
journal contains at most two absolute writes, or one write and one deletion.
Recovery rolls forward; it never adds counters to an existing record. Before
journal publication, prior files remain authoritative. A crash during publication
may leave a complete intent that recovery applies; no player files are modified
until journal publication and directory sync succeed. Startup must complete any
valid pending intent before accepting clients. Invalid or unreadable journals block
startup rather than being discarded.

Filesystem errors halt gameplay and produce a nonzero server exit. The original
files remain authoritative if no journal was published; otherwise a pending result
may be recovered on restart. This does not guarantee preservation of unsaved
in-memory training/combat state from before a prepared outcome.

The journal is native-ABI/version dependent and checksummed for accidental
corruption, not authenticated against an operator controlling the OS account.
Use a filesystem supporting file and directory fsync. Backups must still include
characters, account ownership metadata and any pending storage journal together,
using a stopped server or a coordinated filesystem snapshot. This is recoverable
multi-file settlement, not atomic visibility to unrelated filesystem readers.

## Verification / failure log

### Increment 1: storage backend and deterministic recovery tests

Added `player_storage.c/.h` without changing native player record layout.
Canonical snapshots explicitly clear process pointers and padding. The backend
checks its private directory/lock/journal, holds a process lock, checksums prepared
intent, synchronizes directory entries and refuses unsafe player-file targets.
Failures freeze further writes. Invalid intent is retained and blocks opening.

The separate storage suite injects ENOSPC/EIO before journal publication and across
record write/sync/rename, deletion and journal-clear boundaries. Abrupt child
process exits test replay between records and repeated recovery. Additional cases
cover exclusive process ownership, invalid/checksum-corrupted journals, symlinks
and non-regular targets. Fault hooks are absent from production builds and have
no environment-variable or client-command activation.

GCC optimized strict C99: **961 game/runtime + 159 account/security + 479
storage/recovery checks and both TCP suites passed**. Integration into gameplay
and final compiler/sanitizer matrix are the next increments.

### Increment 2: checked saves and fail-closed startup/training

First reproduced the legacy defect with an intact directory at the player-file
target: `FAIL: failed training save cannot announce or commit a successful
departure`. Save now returns status through the storage backend. Fully trained
departure persists a room-0 snapshot before changing live room/status or announcing
success. Input and the main loop stop on storage failure; the server exits nonzero
and orderly storage-only connection release avoids inventing disconnect outcomes.

Startup acquires storage ownership and completes recovery before opening the
listener; lock or recovery errors refuse startup. An additional real-TCP suite
uses production binaries without fault hooks: a failed training save halts,
another process is rejected, unsafe/corrupted pending intent blocks startup, and
corrected fixture storage recovers training while retaining login/ownership.
Native regression also confirms no false success message and input is ignored
after a storage failure.

GCC optimized strict C99: **973 game/runtime + 159 account/security + 479
storage/recovery checks and all three TCP suites passed**. Combat still uses the
old separate settlement calls in this intermediate increment; coupling those
outcomes is the next change.

### Increment 3: recoverable surrender and death

Reproduced the legacy ignored-deletion defect with a nonempty directory at the
victim's path while retaining its original record separately:
`FAIL: failed death deletion cannot free victim or award a live victory`.

Surrender now prepares winner/loser after-images together before changing live
counters, clearing opponent links or freeing either owned connection. Death now
prepares winner progress plus victim deletion before announcing victory, applying
lethal damage or freeing the victim. Failed deletion is never treated as success;
unsafe directories/symlinks are not removed. File replay completes before startup
can load that character. Successful combat still uses the same strict death
threshold, damage, speed and capped progression rules.

Native tests cover blocked death deletion, retained live state, no false kill
announcement, halted further updates, repaired-target restart recovery and a
second idempotent restart. Both disconnect directions inject ENOSPC on the second
record publication; orderly storage release followed by startup completes exactly
one win/one loss without fabricating a kill. The storage suite independently
corrupts only checksum bytes (TCP coverage separately corrupts the magic header)
and covers ENOSPC both during write and flush/sync.

GCC optimized strict C99: **1006 game/runtime + 159 account/security + 479
storage/recovery checks and all three TCP suites passed** before final matrix
verification. The expected-failing reproduction left only disposable test-owned
artifacts; `make clean` removed those fixtures before re-running the complete suite.

## Operator procedure

- Keep the data directory and files owned by the game's OS user; do not make it
  group/world writable. Native storage requires POSIX locking, rename and fsync.
- `.glad-storage.lock` is an advisory lock inode, not a stale PID flag. Never delete
  it to bypass a live server. Stop the competing process instead.
- On a storage error, the server stops with exit status 1. Fix disk capacity,
  permissions or unsafe targets while retaining the journal and player records.
  Restarting the same build completes a valid `.glad-settlement` before listening.
- Do not delete or edit a pending/corrupt journal as a routine recovery step.
  Keep a copy for diagnosis; restore a coordinated, verified backup or investigate
  with the server stopped. Incompatible native ABI/journal versions refuse startup.
- `.glad-tmp.*` files can remain after abrupt termination. They are not authoritative
  and are not automatically swept. Retain them during investigation; any later
  cleanup must be restricted to confirmed test/orphan files with the server stopped.
- Back up pending intent together with characters and account DB/ownership metadata.
  Do not copy just SQLite's main database while WAL is active.

Physical power loss, filesystem/hardware dishonesty about fsync, production load,
real legacy-player migration and actual disk exhaustion remain outside these
tests. Abrupt-process exit and deterministic ENOSPC/EIO injection are exercised;
no claim of exhaustive crash durability or global account/gameplay atomicity.

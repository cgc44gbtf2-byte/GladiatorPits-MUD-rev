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
Recovery rolls forward; it never adds counters to an existing record. A crash
before the durable journal leaves prior files authoritative. Once intent has been
published, startup must complete it before accepting clients. Invalid or unreadable
journals block startup rather than being discarded.

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

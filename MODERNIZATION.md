# Gladiator Pits modernization

## Scope and compatibility contract

This repository is the 2000-era Gladiator Pits C source release. It contains
`glad.c`, the included command implementation in `commands.c`, and the original
`glad.txt` design guide. There are no external runtime dependencies, dependency
manifests, bundled player saves, area files, or existing automated tests.

The implemented game systems to preserve are the five primary stats (STR, DEX,
STA, SIZ, WIT), character creation and binary player save/load, training,
stadium/training/arena room behavior, public and room chat, WHO/score/commands,
challenge/accept, and turn-based PvP combat. Races, classes, abilities, items,
and explorable areas are not present in this source release; they are not being
invented as part of a compatibility-focused maintenance pass.

## Phased plan

1. **Baseline and contract tests**
   - Record the starting build and compiler diagnostics.
   - Add a portable test target and regression checks for existing commands,
     character stat allocation, player persistence, and combat lifecycle.
   - Move to C99 for standard bounded formatting and typed prototypes without
     changing game rules or the native player record layout.
2. **Build and portability**
   - Make compiler flags, clean, test, and sanitizer targets explicit.
   - Resolve modern compiler diagnostics without changing game calculations.
3. **I/O and socket safety**
   - Bound formatted output and handle partial/interrupted/failed writes without
     buffer overflow, undefined state, or an infinite flush loop.
   - Check socket setup and use modern socket-length types.
   - Preserve line-oriented command parsing and all user-facing game commands.
4. **Persistence and access hardening**
   - Validate player file reads/writes and avoid serializing live process
     pointers while retaining the legacy binary record layout.
   - Replace the source-exposed shutdown password with an environment-configured
     credential while retaining the shutdown command.
5. **Revalidation and documentation**
   - Run the test suite, strict compiler checks, and sanitizers after each
     implementation increment; record failures and remaining risks below.
   - Do not remove or silently migrate game content or player records.

## Audit baseline

- Repository head: `ce27ba8` (`Ensure buffers are really flushed`, 2021-10-04).
- `make` builds `glad` with GCC 15.3.0; the original Makefile has no `clean`
  target (`make clean all` fails before compiling).
- A strict syntax-only pass succeeds but reports unused command parameters,
  a non-standard `__FUNCTION__`, signed-size conversions, misleading
  indentation, and socket address-length type mismatches.
- The repository contains only the six tracked source/documentation files;
  no automated test suite or CI workflow is configured.
- Existing game behavior and player data are to remain intact. In particular,
  this work does not change stat formulas, training limits, challenge rules,
  combat outcomes, death/progression semantics, or the on-disk player record
  size and field order.

## Change and verification log

The plan above was recorded before implementation. Changes were then made and
checked in the following increments:

| Increment | Changes | Verification |
| --- | --- | --- |
| Test/build baseline | Added configurable compiler/linker flags, clean/test targets, and isolated regression fixtures. | Baseline `make` passed; the first fixtures needed corrections described below. |
| Output safety | Replaced unbounded formatting; removed the uninitialized flush control and retry-on-error loops; retained the unsent suffix after short writes; ignored SIGPIPE; bounded recipient queues and fixed per-recipient prompt state. | 27 checks passed normally and with ASan/UBSan. |
| Socket/input portability | Checked socket/bind/listen failures; used `socklen_t`, correct headers, typed callbacks and prototypes; bounded descriptor sets and selected only through the highest live descriptor; processed batched input lines and rejected oversized lines; deferred quit cleanup to avoid accessing a freed connection. | 42 checks passed normally and with ASan/UBSan. |
| Player/access safety | Validated names before allocation; validated full player records before attachment; retained corrupt/incompatible files unchanged; made saves atomic using a temporary file, file flush/fsync and rename; cleared only the serialized opponent pointer; replaced the public shutdown credential with `GLAD_SHUTDOWN_PASSWORD`; widened status arithmetic. | 63 checks passed normally and with ASan/UBSan, including native-layout reload and progression. |
| Challenge lifecycle | Detached both ends when replacing or abandoning a challenge, restored idle status, and required reciprocal challenger links before accepting. Removed remaining unused-parameter diagnostics. | 70 checks passed normally and with ASan/UBSan. |
| Backpressure | Made listener/client sockets nonblocking; watched pending output for write readiness; retained queued data on EAGAIN; disconnected an overfull slow-client queue instead of blocking the game loop. | 74 checks passed normally and with ASan/UBSan, including saturated socket buffers. |
| End-to-end and compiler validation | Added the real TCP smoke suite, all training commands, chat/emote routing, permission checks, title progression and existing permadeath behavior. Made command masks nonnegative and fully initialized the command-table sentinel for strict Clang builds. | 100 checks and the TCP smoke suite pass with GCC 15.3.0 and Clang 20.1.8, both normally and with ASan/UBSan and warnings treated as errors. |

No third-party runtime dependency was added or removed. The body structure's
fields, order, and native layout are unchanged. Original copyright notices and
the historical `glad.txt` are retained. No bundled game content was removed and
no live player files were accessed. Test saves, including the permadeath fixture,
exist only in disposable test directories.

### Failures observed and resolved

- The initial `make clean all` failed because the old Makefile had no clean
  target; the original `make` itself compiled successfully.
- Initial fixture assumptions about name casing and starting stat values were
  incorrect. A defeated combat fixture also used static storage even though
  the game owns and frees defeated bodies, causing an invalid free. Those were
  test-fixture errors, not claims that the original combat rule was broken.
- A baseline sanitizer run timed out while exercising output. The original
  output routine had an uninitialized flush variable, missing loop braces,
  and loops that could retry forever on a failed write. The revised output and
  backpressure tests pass without a hang.
- Clang's strict first pass found incomplete command-table initialization and
  signed enum-mask conversions; a subsequent pass found two test-only enum
  conversions. All were corrected and both compiler matrices now pass.
- Plain `git diff --check` treats the original `glad.c` CRLF endings as trailing
  whitespace. The existing endings were preserved rather than rewriting the
  file; `git -c core.whitespace=cr-at-eol diff --check` passes.

### Reproduction

```sh
make clean all check
make sanitize
make clean all check CC=clang
make sanitize CC=clang
```

The final matrix also adds `-Werror` to the default warning flags. The C suite
checks creation, invalid names, native-layout persistence, training budgets and
all stat commands, command permissions, room/world messaging, challenge
replacement, accept/combat/permadeath, progression titles, long input/output,
TCP line batching, safe quit, failed writes, socket backpressure, and shutdown
configuration. The smoke suite runs the actual server and verifies two-player
creation/training, room chat, challenge/accept/surrender, save/reconnect and
configured shutdown.

## Remaining issues

- **Player authentication is still absent.** Anyone who knows a character's
  name can load it. Adding authentication requires an explicit compatibility
  design for existing characters; it was not silently introduced in this pass.
- **Transport remains plain TCP.** TLS, full Telnet negotiation, IP rate limits,
  idle timeouts and an administrative identity model are not implemented.
- **Legacy saves are ABI-dependent.** Native integer sizes, endianness,
  padding and pointer size are still part of the old record format. This pass
  preserves the current layout, not a portable migration format. Real 32-bit,
  cross-endian, or modified-server save fixtures were not provided; incompatible
  records are preserved and rejected, not guessed at or overwritten.
- **Player files still live in the working directory.** Run in a dedicated
  private data directory and retain backups. Save failures are logged; a full
  user-facing recovery/backup policy and crash-durability tests are future work.
- **The original deathmatch rules remain destructive by design.** A combat
  death removes the loser's character record; this existing game mechanic was
  preserved and tested only on disposable records, not changed into a new rule.
- **Scaling limits remain deliberate.** `select` is capped at `FD_SETSIZE`;
  formatted messages are bounded to `MAX_BUF-1` bytes and slow clients with a
  full output queue are disconnected. Combat is still the original wall-clock
  one-second pulse, without a load/performance benchmark.
- **Platform coverage is bounded.** Linux x86-64 with GCC 15 and Clang 20 was
  executed. macOS/BSD, native Windows, 32-bit systems and production deployments
  were not tested. There is no new GitHub Actions workflow in this pass.
- **Original expansion ideas are not missing implementations to fabricate.**
  Races/classes, items/equipment, mobs, explorable areas, online help and expanded
  advancement are absent from this repository. The five stats, initial training
  budget, wins/losses/kills and rank titles are the progression that exists here.

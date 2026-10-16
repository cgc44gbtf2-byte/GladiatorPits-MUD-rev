# Bounded multiplayer soak

## Scope and method

The opt-in `tests/soak.py` uses production game/admin binaries, ephemeral ports
and a new private temporary data directory. It forces IPv4 loopback and generates
only synthetic accounts/characters. No live player data, deployment, role policy,
combat formulas or native save format is changed. Authentication limits remain
enabled: separate loopback source addresses represent different players rather
than changing the server's per-IP/global limits.

The standard five-minute scenario has:

- 24 trained gladiators, one authenticated administrator and seven anonymous
  spectators: 32 steady connections and 12 concurrent arena pairs.
- Two arena pairs allowed to fight to real, random permadeaths. The loser starts
  a new trained life through ordinary commands; old progression is not restored.
- Ten pairs re-entering duels after alternating graceful `quit` and TCP-reset
  surrender rounds. Both saved counters must settle exactly once; no kill is
  fabricated. Reconnected owners load the existing character.
- A 12-login burst against the unchanged eight-slot worker queue. Only successful
  authentication or explicit busy rejection is accepted, never a silent failure,
  incorrect password result or unexpectedly exhausted global/IP budget.
- A tiny-window client, verified accepted by greeting and descriptor growth,
  then left unread while issuing WHO requests until the server reclaims its
  descriptor. Other clients must remain responsive. This is bounded local pressure,
  not an unrestricted public-server denial-of-service benchmark.
- Anonymous connection/WHO/quit churn and repeated concurrent WHO/response probes.
- Linux `/proc` RSS, descriptors and thread samples every second after warm-up.
  Final descriptor count must return to baseline (one transient log FD tolerated);
  threads remain main+auth worker; final RSS growth must remain below 64 MiB.
- Orderly shutdown while duels remain active, without awarding shutdown surrender
  counters, then a restart that verifies every owner, role, native record and
  surviving counter. A dead final life remains absent; creating a new life has
  base stats/zero counters. SQLite integrity and expected row counts are checked.

At each surrender/death, native saved stats/counters/pointer normalization are
checked against independently tracked expected outcomes. No real passwords,
hashes or account DB contents are printed. Failures stop the run and disposable
directories are removed, including on exceptions.

## Run

```sh
make clean
make CC=gcc CFLAGS="-O2 -Werror" all
make CC=gcc CFLAGS="-O2 -Werror" soak-test

make clean
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
make CC=clang \
  CFLAGS="-O1 -g -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  soak-test
```

`SOAK_SECONDS=300` is the default; use 15..3600 seconds. Runs of at least 120
seconds require actual natural deaths and at least two surrender rounds. A
15-second pilot checks the harness/setup/restart only, not sustained combat.
`make clean` removes generated build/test artifacts, not production data, but
always run these commands in a separate source/build checkout.

The soak is deliberately **not** added to `make check` or the four required
GitHub jobs: those still run their complete native/security/recovery/TCP suites.
The soak prints a compact JSON summary; credentials/raw account data are omitted.

## Interpretation and limits

RSS includes allocator/SQLite caching and transient Argon2 memory; sanitizer
quarantine/shadow memory also changes RSS. The growth threshold is a coarse alarm,
not a leak proof. ASan/UBSan plus leak detection on normal exit provide additional
evidence only for exercised paths. Reported peaks are one-second samples, not
continuous maxima; latency statistics include setup/restart and control requests.

This is not a 24-hour endurance test, supported-player capacity certification,
public/WAN/TLS latency measurement, real-player migration rehearsal, physical
disk-full/power-loss test or broad network penetration test. It does not exercise
every staff mutation or future world editor. The fixture restarts cleanly; abrupt
storage failure/replay is covered separately in `STORAGE_RECOVERY.md`.

## Execution results

The initial optimized 15-second pilot passed setup/restart checks. Review found
a race in the new slow-reader probe: TCP connect/send could return while the
connection was still waiting for application acceptance, making the descriptor
check pass too early. The first attempted long run was intentionally interrupted
(controller exit 130) and discarded, not reported as a game failure or a pass.

The corrected probe waits for the welcome message and verifies descriptor growth
before flooding, then requires server-side reclamation before the client closes.
The corrected sanitizer 15-second pilot passed: 32 steady clients, 12 arenas,
8 accepted/4 busy burst logins, 15 anonymous reconnects, 41 descriptors/two threads
before/after, RSS 22860 -> 23292 KiB, empty sanitizer stderr and verified
restart/ownership/progress. Short pilots have no natural death/surrender round and
are not counted as five-minute combat soaks.

The complete local GCC/Clang optimized and ASan/UBSan regression matrix also
passed: 1006 game/runtime + 159 account/security + 479 storage/recovery checks
and all three TCP suites per entry. This remains separate from the opt-in soak.

### Completed five-minute profiles

All three profiles exercised the same production sources at draft PR #6 head
`239276ab93078c03d3643f652e25be09f6879958`, with no game-source changes in this
follow-up. GCC 15.3.0 used `-O2`; Clang 20.1.8 used `-O1 -g`, ASan/UBSan,
nonrecovering UBSan and frame pointers. Native records remained 72 bytes.

| Profile | Seconds | Surrenders | Natural deaths | Anonymous churn | RPC p95 / max (ms) | RSS baseline -> final (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GCC optimized | 300 | 140 | 18 | 285 | 42.704 / 356.282 | 5180 -> 5180 |
| Clang ASan/UBSan, normal quarantine | 300 | 140 | 18 | 285 | 42.950 / 473.642 | 22752 -> 57380 |
| Clang ASan/UBSan, no-quarantine diagnostic | 300 | 140 | 18 | 290 | 42.712 / 335.158 | 13624 -> 15180 |

Every profile completed 70 graceful and 70 TCP-reset fight disconnects, with
exactly tracked saved counters, zero fabricated kills, normal staff shutdown and
verified restart/ownership/native bytes/SQLite integrity. Descriptors returned
**41 -> 41**, thread counts **2 -> 2**, and the accepted slow reader was reclaimed
by the server before the test closed its client. Every 12-login burst produced
**8 accepted / 4 explicit busy** results. All normal server/admin exits had empty
stderr; ASan/UBSan/leak detection reported no findings in exercised paths.

The primary profiles had 2660 RPC samples each; the diagnostic had 2697. Sampled
RSS peaks were 70716, 118812 and 80784 KiB respectively; sampled FD peaks were
42 in every profile. Peak Argon2 allocations are not permanent retention.
The completed profiles ran sequentially; the concurrent local regression matrix
had finished before these corrected runs, not during their measurements.

### Memory-retention investigation

The instrumented process retained **34628 KiB (~33.8 MiB)** above baseline after
five minutes, versus no net RSS growth in the normal optimized process. This was
not silently dismissed as a leak-free result.

Runtime configuration confirmed the normal ASan quarantine is **256 MiB**, with
**1024 KiB** thread-local quarantine. An additional full five-minute diagnostic
used the same sanitized binaries and scenario with:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:quarantine_size_mb=0:thread_local_quarantine_size_kb=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

Retention dropped to **1556 KiB (~1.5 MiB)**, with no sanitizer findings and the
same descriptor/thread/outcome checks. This supports quarantine/instrumentation
as the main explanation for the larger instrumented RSS, not a confirmed game
leak. It does not classify every retained allocation or prove long-term bounded
memory. Disabling quarantine reduces use-after-free detection opportunity:
**the diagnostic does not replace the primary full-quarantine safety run**, and
normal safety/CI instructions keep quarantine enabled.

No additional game defect was established by these runs, so this commit changes
only test infrastructure/documentation. Hosted regression results for the new
test/documentation head are recorded in the PR verification body; the soak
measurements above are local, not misrepresented as hosted-soak executions.

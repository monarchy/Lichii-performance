# Ocarina of Time CPU experiments, 2026-11-07

Stable 60 FPS has **not** been demonstrated. These changes stay on an experimental branch;
the playable `nx2` branch remains at `1692c074` until a replacement is validated.

## Completed short screen

Build `b6718c0a671108b9f772b8fcc002fabd6b112a1c`, Ryzen 7 7800X3D / RTX 4070 SUPER,
Vulkan, handheld 1X, SMAA, ScaleForce, original effects and existing game patches, 100% speed.
No frame generation or native draw discards. GPU profiling and detailed frame tracing were off.

The opt-in experiments were changed in one process, with a stationary camera in a demanding
Kokiri Forest scene. Each phase settled for 15 seconds, then measured two 10-second intervals.
FPS is the native present-counter difference divided by monotonic host elapsed time.

| Phase | Entry wait | Direct UBO | FPS |
|---|---:|---:|---:|
| Base 1 | Off | Off | 39.444 |
| Entry | On | Off | 40.144 |
| Base 2 | Off | Off | 38.944 |
| Direct | Off | On | 39.944 |
| Base 3 | Off | Off | 39.743 |
| Both | On | On | 40.644 |
| Base 4 | Off | Off | 39.394 |

The four baseline phases aggregate to 39.381 FPS, versus 40.644 for the combined phase
(+3.21% observed). This short, unrandomized screen is useful for selecting experiments; it does
not establish a general speedup, minimum FPS or confidence interval. Background interference
was not completely excluded.

In a second process with asynchronous presentation enabled, the same-camera comparison was
40.994 FPS with both flags off and 42.471 FPS with both on, using the same settling and
measurement lengths. Camera yaw may differ between processes, so this does not isolate the
effect of asynchronous presentation. Do not add these percentages as independent gains.

Only private test profiles were used. Test processes required forced shutdown after ordinary
close attempts failed; their native frame-history CSV did not flush. The completed counter
intervals remain available, but there are no new p99 or 1% low claims for this screen.

## What the switches do

- `EDEN_NX2_MME_ENTRY_WAIT=1`: the clean front MME read waits for all preceding entries to be
  consumed, without waiting for the idle hook to finish. This grants no scheduler or cache
  ownership. Dirty GPU downloads and query tracing retain full drains.
- `EDEN_NX2_UBO_DIRECT=1`: small uniform comparisons use a direct contiguous span, falling back
  to a scratch read for fragmented ranges. Copy the cached snapshot before mapped allocation,
  then stream from that snapshot, preserving data if guest memory changes during allocation.
- `EDEN_NX2_FRONT_BATCH=1`: based on upstream `ed6632e258`, dispatch eligible incrementing
  native Maxwell register packets as runs. Every word still goes through the same register
  semantics. A packet retains its flag across fetched segments; macro, puller, compute, DMA
  and increment-once traffic use their original paths.
- `EDEN_NX2_KERNEL_TIMED_WAIT=1`: Windows-only HostTiming experiment based on
  [upstream PR #538](https://github.com/Djihads80/Eglantine/pull/538). Compare interruptible
  kernel timers with the existing hardware waits, using the same steady deadline and signal.
  It changes no guest clocks. This option requires deadline/probe and same-scene FPS checks.

All default off. The loopback debug server exposes `perf_experiments`; validate all supplied
boolean fields before one atomic update. Unknown fields and non-booleans change nothing.
No game clocks, render counts, effects, resolution or accuracy settings are exposed by it.

## Upstream integration and comparison limits

The next candidate integrates upstream `ae9d3eb8` after checking the actual changes, retaining
the fork's bounded pipeline prebuild, native emission budget, SSA fixes and hardened audio.
It also fixes compute pointer-slot rebuilds and expands compute storage bindings. Own-frame
fence waiting replaces the earlier previous-frame default for Ocarina to prevent exposure
flashes. These change correctness, synchronization and sometimes actual rendered work;
their combined FPS must be measured afresh, not inferred from the preceding table.

The upscaler network-to-copy substitution remains off. Higher FPS obtained by dropping its
work would not satisfy this experiment's graphics constraint.

Private cache version 54 retains the fork's mesh-inclusive captured environment layout, while
invalidating compiled pipelines for changed shader semantics. It does not relabel v52/v53
cache files as v54. The offline parser and CLI explicitly support the private v54 format.

Usage tracking covers partial 64-byte granules conservatively. Same-page overlap queries now
check the requested interval rather than an unrelated used prefix of that page, which could
unnecessarily forbid upload batching. Independent interval/granule tests exercise tracking,
queries, resets, boundary crossings and wrapped ranges. This is not yet a measured FPS claim.

`EDEN_CLI_MAX_SECONDS=1..86400` bounds opt-in SDL runs after emulation starts. SDL's normal
shutdown can flush caches and compiler profiles; callers must still bound startup and possible
guest relaunches externally. With the variable absent, normal operation has no run deadline.

## Further independent checks

The generic build used in the short screen has LTO off. The next local candidate uses
`-march=native -ffp-contract=off` and ThinLTO, with one linker and two LTO workers to bound memory.
It is specific to the test PC's exposed CPU capabilities. Local compiler probes also verified
instrumentation/profile-use support. No profile-trained emulator build has yet been measured.
Compiler capability is **not** a measured game speedup; the candidate needs correctness tests
and a fresh same-quality game comparison.

The compiler techniques are described in the official [Clang ThinLTO documentation](https://clang.llvm.org/docs/ThinLTO.html)
and [profile-guided optimization manual](https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization).
No fast-math option is part of this plan.

## Native ThinLTO screen completed later that day

Local source `c17a6f5113` (GUI SHA-256 `c1ddf4c8097e2a671ef4d78f95ad70d49e14321d0943b25f2f822cecd88ba165`), same graphics listed above and own-frame fence waits. The private profile used no physical controller bindings; debug input loaded the copied save and positioned the camera. One process, 15 seconds settling plus 20 seconds measurement per phase.

| Phase | Entry + direct UBO + packet batch | Kernel timer | FPS |
|---|---:|---:|---:|
| base-1 | Off | Off | 40.495 |
| kernel-1 | Off | On | 39.156 |
| base-2 | Off | Off | 40.665 |
| gpu-all-1 | On | Off | 41.916 |
| all-1 | On | On | 39.919 |
| gpu-all-2 | On | Off | 41.546 |
| base-3 | Off | Off | 40.495 |

Aggregate baseline: 40.552 FPS. Aggregate three-command-path phases: 41.731 FPS (+2.91% observed). The kernel timer was slower in this screen, including with the other three changes. All switches were restored off. **60 FPS remains unachieved.** No whole-game minimum, statistical significance or cross-build speedup is established by these short, unrandomized samples. The stationary camera differs slightly from older processes, so percentages are not added across rounds.

Both test GUI runs closed normally and flushed native frame-history CSV. That file has no phase timestamps, so no heavy-phase p99 or 1% low is claimed. Original saves were unchanged.

Validation of this native source: 823 C++ cases, including the 808-case main suite in both lexical and the previously used seeded order; 31 CLI tests; 25 Python parser/archive/container/static tests; four captured regression fixtures validated with SPIR-V tools; maximum-resolution deadline probes with both default and kernel-timer backends. All passed. A profile-trained release build has not yet been measured. Instrumentation FPS will not be used as a release baseline.

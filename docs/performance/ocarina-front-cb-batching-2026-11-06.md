# Ocarina front constant-buffer batching: no demonstrated FPS improvement

The bounded front constant-buffer batching prototype did not demonstrate an in-game FPS
improvement. Keep the previously validated `1692c074` build as the playable version. This
experiment is retained on a research branch; it is not a 60 FPS release or a recommended
replacement for `nx2`.

## Measured result

After an interrupted measurement was discarded, four fresh portable processes ran in ABBA
order. A was `1692c074`; B was the local prototype `62573adc` (source tree
`fef15ebfedde4cb888a107dee289c3ccfacf6c2f`). Both GUIs used Clang 23.1.5 and the same runtime
dependencies. Each process loaded a separate copy of the same frozen save and cache, replayed
the same 100-present right-stick input, settled for 15 seconds, and measured 60 seconds with
no control input. No builds, tests or diagnostic profilers ran during these timed phases.

| Run | Guest FPS | p99 frame ms | 1% low FPS | Longest frame ms |
| --- | ---: | ---: | ---: | ---: |
| A1: validated build | 40.068 | 31.606 | 28.750 | 44.624 |
| B1: batching prototype | 40.237 | 29.641 | 31.909 | 35.298 |
| B2: batching prototype | 40.333 | 30.779 | 30.725 | 36.390 |
| A2: validated build | 41.892 | 28.896 | 32.433 | 40.405 |

Combined guest presents / elapsed time was **40.980 FPS for A** and **40.285 FPS for B**
(observed difference **-1.70%**). Baseline variation between A1 and A2 is larger than this
difference. Camera yaw and animated foliage were not pixel-identical despite the same input
sequence. These observations do not establish a causal regression or a small improvement.
The frame-time ranges also overlap; the first baseline's slower tail did not repeat in A2.
There were no frames over 50 ms in these four measured steady phases. This says nothing
about first shader compilation or other locations. A minimum of 60 FPS was not achieved.

A separate, shorter screening run of the existing split mode `1` on the validated build
(macros on the back thread) produced **38.323 FPS over 20.014 seconds** after the same settling
period. This was one screening pass, not another ABBA experiment. It supplied no reason to
replace the default split mode `2`.

## Conditions and measurement limits

- Ryzen 7 7800X3D, RTX 4070 SUPER, 32 GB RAM, Windows; NVIDIA driver 621.01.
- One stationary Kokiri Forest view close to the player and foreground foliage. This is a
  local stress view, not an entire-game benchmark or a movement/cold-compilation test.
- Graphics were preserved: Vulkan, Handheld, 1X internal resolution, SMAA, ScaleForce, existing
  game accuracy and Mailbox settings. The existing Max Resolution and Dynamic FPS patches
  stayed enabled. Game speed stayed at 100%; frame generation stayed off.
- Both sides used split mode `2`, native draw flush budget `128`, normal process priority and
  the original Info log filter. GPU profiling and frame traces stayed off.
- Fourteen save-file hashes matched before preparation; the real playable profile was not
  used for tests. Its fourteen save hashes still matched before normal play was restored.
- Primary FPS is the difference in the emulator's native present counter divided by measured
  wall time. Native frame history, collected by both builds even when recording is disabled,
  was written only at normal shutdown. CSV intervals discard the initial five frames and
  use the measured present indices. Their durations agree with counter intervals within
  0.034%. That agreement is a cross-check, not independent evidence of scene equivalence.
- p99 is the nearest-rank 99th percentile. 1% low is 1000 divided by the mean of the slowest
  ceiling(1% of frames) frame times in milliseconds. Individual runs are reported so drift
  and overlap remain visible. The aggregate CSV contains no saves, game assets or raw logs.

## Prototype implementation

The idea is adapted from [Eglantine PR #503](https://github.com/Djihads80/Eglantine/pull/503),
with this fork's register/shadow semantics preserved. Up to 64 existing CB_DATA method
entries are staged before reserving ring space. The staged entries retain the original
method and argument; the existing back-end CB coalescer executes them. Flushes precede
engine changes, other executable state writes, publication, drains, writer/sync waits and
list completion. Shadow Replay keeps the existing path. Pending state resets between
sessions. No shader arithmetic, graphics settings, draw skipping or game clocks changed.

Correctness validation before the live comparison:

- 799 normal C++ test cases passed: 784 main, 8 update and 7 configuration cases.
- Six focused split/dispatch cases passed 2,271 assertions. Coverage includes ring wrap,
  engine/order boundaries, worker waits, session reset and Track-to-Replay equivalence.
- A separate hidden enqueue benchmark passed 5,900 assertions. Five alternating-order
  repetitions with 1,024 bursts per repetition gave these medians in ns per burst:

| CB words | Existing MacroWrite | Queued CB path |
| ---: | ---: | ---: |
| 2 | 8.203 | 7.422 |
| 3 | 13.184 | 12.109 |
| 16 | 29.492 | 20.703 |
| 64 | 157.520 | 105.371 |

The enqueue benchmark has no concurrent worker, guest execution, Vulkan driver or scheduler
contention. Its distributions overlap, and both paths use the candidate executable. It
does not include every added empty-pending check against the original executable. Faster
enqueue in this model did not translate into a demonstrated game improvement.

The hidden benchmark can be selected explicitly with `[gpu_split_cb_batch_bench]`; it is
excluded from normal test selection. The hidden benchmark was added after the prototype GUI
build; the GUI implementation used for the live runs is the tree identified above. No new
public binary is released from this experimental branch. The normal saved game was restored
on `1692c074`, with profiling, traces and frame-history file recording disabled.

Per-run data: [aggregate CSV](data/ocarina-front-cb-batching-2026-11-06.csv).

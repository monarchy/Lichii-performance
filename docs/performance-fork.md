<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Performance fork: changes and validation

Date: 2026-11-06. Repository: [monarchy/Lichii-performance](https://github.com/monarchy/Lichii-performance).
[Русская версия](performance-fork.ru.md).

## Baseline and provenance

The installed build under investigation is Lichii build 88 (`55532f2d814215aaf32a76c214cf129f2cb6282b`).
Development starts at Lichii `588bfa611be9853729682dd78416c66e8d56385a`, so the intervening
Lichii changes are upstream work, not optimizations authored in this fork.

Before new work, Eglantine's `nx2`, open/closed pull requests, branches and relevant issues
were surveyed. The local integration includes `0857e8b4a792ac99b87af6bb6f329127aa7df683`.
Publication may represent that integration as an attributed port rather than importing
the independent repository's complete Git object history.

The MaxContinuousRange cache proposal in Eglantine
[#670](https://github.com/Djihads80/Eglantine/pull/670) is already superseded by Lichii's
map-time continuous-range index. The upload-scan/index-bind work in
[#562](https://github.com/Djihads80/Eglantine/pull/562) is also already covered by newer
Lichii paths. Neither is copied again. The preload-all proposal
[#696](https://github.com/Djihads80/Eglantine/pull/696) was reverted by
[#698](https://github.com/Djihads80/Eglantine/pull/698); this fork does not restore it.

## Imported upstream corrections

### GPU scheduler ownership

[Eglantine #639](https://github.com/Djihads80/Eglantine/pull/639) fixes a race in which the
split GPU back thread had published command consumption but was still submitting deferred
work through its idle hook. `Drain()` now also waits for the idle hook to finish. The
calling GPU thread is marked explicitly, and buffer-cache operations that record GPU work
require scheduler ownership.

The integration preserves Lichii's per-site drain profiling, batched method execution,
native storage-buffer binding caches and inline page writes. A diagnostic variable that
exists only in upstream's older implementation is not imported. This is a synchronization
correction, not an asserted FPS improvement.

The merged Lichii backoff helper originally had a sleep predicate only for unconsumed
commands. Once commands were consumed, a delayed idle hook would leave it repeatedly
checking atomics after the initial spin budget. The follow-up yields in that case, keeping
the existing completed-command condition and idle-hook completion check intact.

### Guest-memory lifetime

[Eglantine #634](https://github.com/Djihads80/Eglantine/pull/634) releases Switch 2 guest RAM
after the game has stopped and GPU, host1x, CPU and kernel teardown have completed.
`InitializeKernel()` recreates it before the next session's initialization check.
This can reclaim memory between sessions; it does not reduce a running game's required RAM.

### Migration strategy selection

The upstream migration-dialog correction associates each option with its explicit strategy
enum rather than assigning incrementing IDs. This avoids selecting a different strategy
through an ID mismatch. Lichii's existing `eden` data path is retained; the Eglantine data
directory rename and unrelated PR-retarget workflow are excluded from the integration.

## New bounded Vulkan successor cache

Previously each graphics pipeline kept two growing vectors: complete copies of successor
`GraphicsPipelineCacheKey` objects, and the matching pipeline pointers. Every transition
searched those copied keys linearly. A predecessor with many successors could keep growing
its allocation and scan length.

The replacement keeps 16 non-owning pointers to existing pipelines. It compares their
immutable full keys using the existing equality operator. The first 15 alternatives remain
cached; the final slot follows later misses. This retains useful hits in a capacity-plus-one
cycle, unlike a FIFO ring that can miss every lookup in that workload.

The unchanged self-transition shortcut runs first. A miss still reaches the authoritative
graphics-pipeline map, so forgetting a successor does not drop a draw or change the selected
pipeline. Existing eviction clearing is retained before an evicted pipeline is destroyed.

Properties on the tested 64-bit build:

- At most 16 successor comparisons, plus the existing self-key comparison.
- A 136-byte successor-cache object, with no per-transition allocation or full-key copying.
- The previous empty pair of vectors was 48 bytes: empty pipelines therefore cost 88 bytes
  more, while dynamically growing transition-key allocations are eliminated.
- No shader-cache version change, shader rejection, draw skipping or game-timing patch.

## Validation status

| Check | Result |
| --- | --- |
| Focused successor-cache regression suite | Passed: 5 cases, 12,316 assertions |
| Synthetic lookup benchmark | Completed: five alternating-order repetitions per workload |
| Real split-worker idle-hook regression | Syntax check passed; runtime check pending full build |
| Full Windows production build | In progress; not yet claimed successful |
| Full emulator regression suite | Pending full build |
| Modified executable in Ocarina gameplay | Not run; the existing game has unsaved progress |

The focused cases exercise full key identity, randomized fallback across overflow and
clearing, clearing before successor destruction, restoration of full capacity after a
clear, capacity-plus-one locality and repeated-add safety.

The integrated `[gpu_split_idle_hook]` case uses the actual split worker, a deferred command
and a deliberately blocked idle hook. It checks `Drain()` completion separately from
`Stop()`, verifies scheduler ownership and repeats a full worker restart. It does not boot
a game or test the active emulator process.

## Synthetic benchmark

Environment: Windows 11 x86-64, Ryzen 7 7800X3D, Clang 23.1.5 targeting
`x86_64-w64-windows-gnu`, `-O2`, Boost 1.92.0. An existing emulator session remained running;
these short measurements are not a controlled machine-wide gaming benchmark.

The benchmark uses a **synthetic 1024-byte key**, six shader hashes and a compared state
prefix. That number is not a measured size of the real pipeline key. Hashing and fallback
lookup use production `Common::CityHash64` and `Common::unordered_map`. Driver work,
synchronization and the other slow-path bookkeeping are outside the model. Each timed
sample performs 10,000 lookups; five samples alternate old/new order.

Selected medians in nanoseconds per lookup:

| Workload | Previous vectors | Bounded pointers |
| --- | ---: | ---: |
| 1 successor, different shader hashes, cycle | 6.07 | 5.66 |
| 2 successors, different shader hashes, cycle | 6.92 | 7.04 |
| 8 successors, shared shader hashes, cycle | 22.38 | 22.36 |
| 16 successors, shared shader hashes, cycle | 42.07 | 42.38 |
| 17 successors, different shader hashes, cycle | 19.21 | 20.67 |
| 17 successors, shared shader hashes, cycle | 44.62 | 46.63 |
| 256 successors, shared shader hashes, cycle | 669.17 | 91.86 |
| 256 successors, shared shader hashes, repeatedly last successor | 1287.58 | 77.36 |

The 17-successor cases regress by about 7.6% and 4.5% in this model. This is an explicit
tradeoff for bounded storage and lookup work; it must be checked against real workloads.
The full [CSV](performance/pipeline-transition-results.csv) includes sample minima and
maxima. **None of these ratios is a game-FPS multiplier.**

## Reproducing the focused checks

With Clang, Catch2 3.x and Boost headers available, from the repository root:

```sh
clang++ -std=c++20 -O2 -Wall -Wextra -Isrc \
  src/tests/video_core/pipeline_transitions.cpp -lCatch2Main -lCatch2 \
  -o pipeline-transition-tests
./pipeline-transition-tests
clang++ -std=c++20 -O2 -Wall -Wextra -Isrc \
  tools/nx2/pipeline_transition_bench.cpp src/common/cityhash.cpp \
  -o pipeline-transition-bench
./pipeline-transition-bench
```

MSYS2 appends `.exe` on Windows. For the integrated suite, build the normal `tests` target
with `YUZU_TESTS=ON`, then run `tests "[pipeline_transitions]"`.

## Windows build and packaging

The local Windows build uses the MSYS2 UCRT64 Clang toolchain and system Qt. Network services
and USB support remain enabled. LTO is off for this validation build. The upstream updater
is disabled so this experimental executable is not replaced by an unrelated official build.

```sh
cmake -S . -B build/performance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_TESTING=ON -DYUZU_TESTS=ON -DDYNARMIC_TESTS=OFF \
  -DENABLE_QT=ON -DYUZU_CMD=ON -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF \
  -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_USE_BUNDLED_SDL3=ON -DYUZU_USE_BUNDLED_FFMPEG=ON \
  -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
  -DENABLE_WEB_SERVICE=ON -DENABLE_LIBUSB=ON -DENABLE_UPDATE_CHECKER=OFF \
  -DENABLE_LTO=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_DISABLE_LLVM=ON
cmake --build build/performance --target yuzu yuzu-cmd tests --parallel 2
```

[package-performance.ps1](../tools/windows/package-performance.ps1) prepares a new package
directory from the built executables, bundled libraries and Qt deployment tools. It checks
the DLL import closure and copies licenses and these change notes. It requires an empty
destination and does not copy a `user` directory, saves, keys, games or application logs.
For example, in PowerShell with a UCRT64 toolchain at `C:\msys64`:

```powershell
./tools/windows/package-performance.ps1 -BuildDirectory ./build/performance `
  -RuntimeDirectory C:/msys64/ucrt64/bin -Destination ./build/performance-package
```

## Gameplay comparison still required

After saving progress, compare the original and modified executables with the same game
version, scene, camera, resolution, GPU accuracy and warm shader cache. Measure frame times
and long stalls separately from the average FPS, and check rendering and game speed.
Keep logs, captures, games, keys, firmware and personal paths local. No gameplay performance
gain is claimed until that comparison exists.

Implementation, investigation, review, tests and this documentation were assisted by OpenAI Codex.

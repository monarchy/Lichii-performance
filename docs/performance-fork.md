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
- The real graphics pipeline key is 1,056 bytes on this ABI, measured with the production
  compiler flags and headers. Each previous retained edge copied that key plus an 8-byte
  pointer (1,064 bytes before vector capacity slack and allocator metadata).
- The previous empty pair of vectors was 48 bytes: empty pipelines therefore cost 88 bytes
  more, while dynamically growing transition-key allocations are eliminated.
- No shader-cache version change, shader rejection, draw skipping or game-timing patch.

## Validation status

| Check | Result |
| --- | --- |
| Focused successor-cache regression suite | Passed: 5 cases, 12,316 assertions |
| Synthetic lookup benchmark | Completed: five alternating-order repetitions per workload |
| Real split-worker idle-hook regression | Passed: 1 case, 30 assertions; two worker sessions |
| Windows GUI, CLI and three test targets | Built successfully with Clang 23.1.5 and LLD |
| Main emulator regression suite | Passed: 775 cases, 69,838,194 assertions |
| Update-checker regression suite | Passed: 8 cases, 99 assertions |
| Configuration regression suite | Passed: 7 cases, 426 assertions |
| Packaged DLL import closure | Passed, including the Qt Windows platform plugin |
| Isolated packaged CLI `--version` smoke check | Exit code 0 with only Windows system directories on `PATH` |
| Modified executable in Ocarina | GUI, game boot and copied-save load succeeded; stationary comparison below |

The tested executable was built from commit
[`0021060964b301fe18cded25491d012531b1c89d`](https://github.com/monarchy/Lichii-performance/commit/0021060964b301fe18cded25491d012531b1c89d).
The three CTest executables passed in 19.48 seconds. A subsequent game check used four
disposable portable profiles. Both modified-build runs booted the game and loaded the copied
save. Fourteen original save files matched their SHA-256 fingerprints after the comparison.
The original emulator process was left open; it was already showing its game list before
these measurements. Broad rendering, save compatibility and gameplay coverage remain untested.

The focused cases exercise full key identity, randomized fallback across overflow and
clearing, clearing before successor destruction, restoration of full capacity after a
clear, capacity-plus-one locality and repeated-add safety.

The integrated `[gpu_split_idle_hook]` case uses the actual split worker, a deferred command
and a deliberately blocked idle hook. It checks `Drain()` completion separately from
`Stop()`, verifies scheduler ownership and repeats a full worker restart. It does not boot
a game or test the active emulator process.

### Corrections to existing tests

The first full run exposed three failures in existing, unchanged tests. Two compared
libswscale's row padding: all active Y/UV bytes agreed, while 24 padding bytes differed.
Scaler tests now compare every active pixel byte, including full/limited/full range changes.
Strict direct-copy, odd-row, storage-bound and sentinel checks remain unchanged.

The NUP metadata test kept its first reader open across a second archive replacement.
Windows refused that replacement under the reader's sharing mode. The test now closes the
reader before editing again. Production video conversion and file-sharing behavior were
not changed to make these tests pass.

## Synthetic benchmark

Environment: Windows 11 x86-64, Ryzen 7 7800X3D, Clang 23.1.5 targeting
`x86_64-w64-windows-gnu`, `-O2`, Boost 1.92.0. An existing emulator session remained running;
these short measurements are not a controlled machine-wide gaming benchmark.

The benchmark uses a **synthetic 1024-byte key**, six shader hashes and a compared state
prefix. That number is not a measured size of the real pipeline key. Hashing and fallback
lookup use production `Common::CityHash64` and `Common::unordered_map`. Driver work,
synchronization and the other slow-path bookkeeping are outside the model. Each timed
sample performs 10,000 lookups; five samples alternate old/new order.
The benchmark sends size metadata to standard error, leaving standard output as CSV.

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
with `YUZU_TESTS=ON`, then run `tests "[pipeline_transitions],[gpu_split_idle_hook]"`.
The complete registered suite is run with `ctest --test-dir build/performance --output-on-failure`.

## Windows build and packaging

The local Windows build uses the MSYS2 UCRT64 Clang toolchain, LLD and system Qt. Network services
and USB support remain enabled. LTO is off for this validation build. The upstream updater
is disabled so this experimental executable is not replaced by an unrelated official build.

```sh
cmake -S . -B build/performance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_LINKER_TYPE=LLD \
  -DCMAKE_JOB_POOLS=link_pool=1 -DCMAKE_JOB_POOL_LINK=link_pool \
  -DBUILD_TESTING=ON -DYUZU_TESTS=ON -DDYNARMIC_TESTS=OFF \
  -DENABLE_QT=ON -DYUZU_CMD=ON -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF \
  -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_USE_BUNDLED_SDL3=ON -DYUZU_USE_BUNDLED_FFMPEG=ON \
  -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
  -DENABLE_WEB_SERVICE=ON -DENABLE_LIBUSB=ON -DENABLE_UPDATE_CHECKER=OFF \
  -DYUZU_USE_BUNDLED_OPENSSL=OFF -DUSE_CCACHE=OFF -DENABLE_WERROR=ON \
  -DENABLE_LTO=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_DISABLE_LLVM=ON
cmake --build build/performance --target yuzu yuzu-cmd tests config_tests update_checker_tests --parallel 4
ctest --test-dir build/performance --output-on-failure
```

[package-performance.ps1](https://github.com/monarchy/Lichii-performance/blob/codex/ocarina-performance/tools/windows/package-performance.ps1) prepares a new package
directory from the built executables, bundled libraries and Qt deployment tools. It checks
the DLL import closure and copies licenses and these change notes. It requires an empty
destination and does not copy a `user` directory, saves, keys, games or application logs.
The MSYS2 runtime's license notices are included when available. A `qt.conf` makes the
packaged `plugins` directory discoverable without the development toolchain.
For example, in PowerShell with a UCRT64 toolchain at `C:\msys64`:

```powershell
./tools/windows/package-performance.ps1 -BuildDirectory ./build/performance `
  -RuntimeDirectory C:/msys64/ucrt64/bin -Destination ./build/performance-package
```

## Stationary gameplay comparison

Measured on 2026-11-06 with a Ryzen 7 7800X3D, RTX 4070 SUPER and 32 GiB RAM. The game was
Ocarina v1.1.1, in the same saved Kokiri Forest interior, with the same camera and no movement.
Settings: Vulkan, 2X, GPU Fast, the existing built-in Max Resolution/Dynamic FPS patches,
the same VSync setting, and frame generation off. Each run started with a fresh copy of the
same save, configuration and disk shader cache, followed by a scene warm-up.

Order: original **A1**, modified **B1**, modified **B2**, original **A2**. The original is
build 88 (`55532f2d`, MSVC 19.44); the modified build is `00210609`, Clang 23.1.5.
No compiler or source-baseline control build was produced, so this compares complete
executables, not the isolated effect of one optimization.

The primary metric is the delta of the built-in debug server's guest-present counter divided
by elapsed monotonic time. Both marks were taken while the game was running and the camera
was stationary. Client and server elapsed times agree within 0.253 ms over these intervals.
This measures guest presentation requests, not physical monitor frames.

| Run | Build | Seconds | Guest presents | Guest FPS |
| --- | --- | ---: | ---: | ---: |
| A1 | Original build 88 | 89.032 | 4,473 | 50.240 |
| B1 | Performance 00210609 | 89.348 | 4,688 | 52.469 |
| B2 | Performance 00210609 | 108.431 | 5,841 | 53.869 |
| A2 | Original build 88 | 97.212 | 4,761 | 48.975 |

The equal-weight mean of two runs per build is **49.6077 → 53.1687 FPS (+7.1782%)**.
This is an observation for this stationary scene, not a guaranteed improvement elsewhere.
The full [aggregate CSV](performance/ocarina-gameplay-results.csv) includes both primary
counter results and the available native frame-time checks.

### Frame-time checks and limitations

The common `record_frame_times=true` setting stores guest-composite intervals in memory and
exports CSV on shutdown. For A1/B1/B2, verified steady-state windows of approximately 60 s
gave 50.110/52.401/54.182 FPS and 1% lows of 35.227/35.476/38.164 FPS. These are shorter
subintervals of the runs above, not independent repeated measurements. The longest frame
was 31.655/31.715/30.209 ms respectively; none exceeded 50 ms in those selected windows.
The pause gap and shutdown frames were identified and excluded, with the boundaries checked
against the recorded pause counters. `1% low` is 1000 divided by the mean of the slowest
ceil(1% of frames) intervals. GPU timestamp tracing remained off.

The A2 original-build copy hung during shutdown after both FPS marks had been recorded.
Its native CSV was not exported; its 1% low, p99 and long-stall counts are unknown. Only
that disposable process was terminated after checking its exact executable path. This
single shutdown failure does not establish a general stability advantage for either build.

Only two runs per build were measured. Available memory at the start of A1/B1/B2/A2 was
2458/5588/4187/4026 MiB; paging snapshots also differed. Desktop background activity and
shared OS/driver cache warm-up were not controlled. The comparison therefore does not
separate compiler, baseline, patch and memory effects. Movement, other scenes, cold-cache
shader stutters, physical display pacing and game-speed correctness remain untested.
Keep raw logs, captures, games, keys, firmware, saves and personal paths local.

Implementation, investigation, review, tests and this documentation were assisted by OpenAI Codex.

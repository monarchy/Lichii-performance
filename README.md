<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Lichii performance

An experimental performance fork of [zortos293/Lichii](https://github.com/zortos293/Lichii),
the open-source Nintendo Switch and Switch 2 emulator descended from Eglantine, Eden,
yuzu and Sudachi. This fork is independent of those projects and Nintendo.

[Описание изменений на русском](docs/performance-fork.ru.md) ·
[Changes, measurements and limitations](docs/performance-fork.md) ·
[Upstream project](https://github.com/zortos293/Lichii)

## Changes

- Port Eglantine's GPU scheduler ownership and idle-hook synchronization fixes.
- Yield during long waits for a GPU idle hook instead of repeatedly polling completed commands.
- Release Switch 2 guest memory after a game stops, and recreate it for the next game.
- Bound Vulkan pipeline successor caches to 16 pointers instead of keeping an unbounded
  collection of full graphics-state key copies.
- Spread retirement of completed Vulkan graphics pipelines across frames, up to 64 per frame.
- Add opt-in timings that distinguish runtime translation, SPIR-V emission, driver compilation,
  cache persistence and pipeline destruction.
- Add focused regression tests and a reproducible synthetic lookup benchmark.
- Correct existing scaler-padding comparisons and Windows archive-reader test lifetimes.
- Keep the existing Eden data directory and Lichii's newer native buffer/shader handling.

The six new cache/worker tests passed (12,346 assertions) on Windows, along with all
790 cases across the main, configuration and update-checker suites. GUI and CLI binaries
were built successfully. The successor-cache object is 136 bytes on this 64-bit build,
with no per-transition allocations.
Synthetic high-fan-out lookups improved, while a 17-successor boundary case became slightly
slower. See the complete [results and tradeoffs](docs/performance-fork.md).

A subsequent four-run stationary Ocarina comparison observed **49.61 → 53.17 guest FPS
(+7.2%)**, averaging two warmed runs per complete build. This is one scene on one PC;
compiler, source baseline and memory conditions differ. Movement and cold-cache stutters
were not tested, and the final original-build run hung during shutdown after its FPS
measurement. See the [measurements and limitations](docs/performance-fork.md#stationary-gameplay-comparison).
Synthetic lookup ratios are not game-FPS multipliers or isolated measurements of this patch.

A separate [stutter investigation](docs/performance-fork.md#stutter-investigation) identified
first-use shader/driver compilation and a smaller burst of pipeline destruction. The retirement
limit addresses the latter; it does not eliminate compilation of new shaders.

## Building and testing

Follow the upstream [build guide](docs/Build.md) and [coding guidelines](docs/Coding.md).
The desktop GUI target is `yuzu` and produces `lichii.exe`; the command-line target is
`yuzu-cmd` and produces `eden-cli.exe` on Windows. Configure with `YUZU_TESTS=ON` to include
the new cases, then run `tests "[pipeline_transitions],[gpu_split_idle_hook]"`.

The [benchmark source](tools/nx2/pipeline_transition_bench.cpp) uses synthetic keys and the
production CityHash64 and Boost fallback container. Instructions and exact environment
are in the [validation notes](docs/performance-fork.md#reproducing-the-focused-checks).

## Existing data

The application retains the existing Eden data directory, settings, saves and shader caches.
Use a backed-up or isolated data directory when trying an experimental build.
Games and system files are not included.

## Credits and license

Original copyright notices and third-party licenses remain in effect. Lichii inherits the
work of the Eglantine, Eden, yuzu and Sudachi contributors, including Eglantine's original
developer Camille LaVey. Upstream fixes are identified individually in the change notes.
This fork's implementation, review, tests and documentation were assisted by OpenAI Codex.
See [LICENSE.txt](LICENSE.txt) and the individual source-file notices.

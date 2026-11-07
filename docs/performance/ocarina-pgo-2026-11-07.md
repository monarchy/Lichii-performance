# Ocarina: native ThinLTO, game-trained PGO and CPU placement

**Stable 60 FPS in the heavy scene has not been achieved.** This is an experimental report, not a release-performance guarantee. The `nx2` branch stays on the previously playable build.

## Build and conditions

Both executables use source `c17a6f5113a33d48e9a2970ef49ce540a81e71b1` (the source tree published by `0a8a9595bd8726ce17be4032e16eed31d7ba6b59`). Clang 23.1.5, native Ryzen 7 7800X3D targeting, ThinLTO, `-O3 -DNDEBUG -march=native -ffp-contract=off`; no fast-math. The second executable uses IR-PGO from a successful private run of this game. RTX 4070 SUPER, 32 GiB RAM, driver 621.01.

Vulkan, handheld 1X, SMAA, ScaleForce, GPU Fast, Mailbox, full original upscaler network, existing Max Resolution and Dynamic FPS patches, 100% game speed, no frame generation or discarded native draws. All four command/timer experimental switches are **off**. The own-frame guest fence policy is retained. Hardware controller bindings are neutralized only in disposable test profiles. Fourteen original save files were verified when preparing each copy.

## Preliminary A then B screen

One native process then one PGO-use process. Each view has 15 seconds settling and 60 seconds measurement. No compiler, test suite, GPU profiling or frame trace runs during measurement. FPS is guest present-count delta divided by monotonic real elapsed time, not physical monitor scanout or a minimum-FPS metric.

| Build | View | Presents | Seconds | Mean FPS |
|---|---|---:|---:|---:|
| native | save-default | 3048 | 60.012024 | 50.790 |
| pgo-use | save-default | 3286 | 60.011253 | 54.756 |
| native | dense-foliage | 2322 | 60.034705 | 38.678 |
| pgo-use | dense-foliage | 2620 | 60.044717 | 43.634 |

The initial view uses the saved camera without camera input. The dense-foliage view is aligned approximately using visible landmarks; exact yaw, wind, character and fairy animation do not match. The PGO initial-view ten-second windows rose from roughly 52 to 57 FPS. Warming, background work and run order are not isolated. This is one unrandomized A/B screen; the table does **not** establish an exact causal PGO percentage, statistical significance or a whole-game minimum. Percentages are not added to earlier rounds.

## Same-process CPU placement screen

Same stationary dense-foliage view in the PGO process; 15 seconds settling plus 20 seconds measurement per phase. Windows core topology supplies the masks: one logical processor from each SMT2 physical core, then the other sibling. Only this disposable process is affected.

| Phase | Process mask | Mean FPS |
|---|---:|---:|
| full-a | `0xffff` | 43.547 |
| one-per-core-low | `0x5555` | 43.647 |
| full-b | `0xffff` | 43.047 |
| one-per-core-high | `0xaaaa` | 44.347 |
| full-c | `0xffff` | 41.765 |

The original `0xffff` mask was restored and verified. This short placement screen did not approach 60 FPS or establish a reliable improvement. It does not disable SMT system-wide or change clocks. The API scope follows Microsoft's [SetProcessAffinityMask documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask).

## PGO provenance and validation

Training uses only the normally completed R9 game process, with default and three-switch command paths and a camera sweep. Failed boots, Vulkan device-probe child processes and private diagnostic profiles are excluded. The merged profile is frozen before the use build. Raw profiles, saves, code images, shaders and captures remain private.

The embedded ZBIC decoder translation unit is explicitly excluded from profile generation and use in both training and this candidate. Entropy-coded synthetic data exposed an upstream strict-aliasing violation (`U32` table storage read through a `short*`); native decoding succeeds, instrumented strict-aliasing decoding fails. A separate draft disabling alias analysis for that translation unit passes all 27 synthetic corpus cases, and the proposed three-case C++ regression fails before the fix and passes after. Those draft fixes are **not included** in the measured executables. The decoder exclusion is a verified experiment workaround, not an FPS optimization.

PGO-use validation passed: 808 main C++ cases (70,011,429 assertions), seven configuration cases (426 assertions), eight updater cases (99 assertions), 31 shader CLI tests, four regression fixtures with SPIR-V validation, and an independent synthetic entropy-decoder check. Both measured GUI runs closed normally. These checks do not prove every game path or eliminate all freezes.

- Native GUI SHA256: `c1ddf4c8097e2a671ef4d78f95ad70d49e14321d0943b25f2f822cecd88ba165`
- PGO-use GUI SHA256: `814eb67a5e96fcae1e9cb1ec74ec35b34c93809f65af69b03265034baf71b836`
- Private merged profile SHA256: `1179c69038be4778fada12af6ad00ab05e7fa20d2a7c25cae834cead671bde09`

The next investigation traces producer/consumer order around the six-word MME conditional-render read. No wait is removed on the strength of this FPS table, and no diagnostic timing is presented as a release gain.

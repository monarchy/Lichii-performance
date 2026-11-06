<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# First-use shader compilation: SSA alias chains

The shader compiler now resolves redundant Identity aliases before storing SSA definitions.
A register-copy sequence previously accumulated a chain that later reads, replacement and
dead-code cleanup repeatedly walked. Definitions now point to the resolved value. Resolution
stops at a live Phi node, so later branch/loop Phi resolution remains observable. Real operand
edges retain the existing use-count tracking; DCE logic and pass order are unchanged.

Local definitions also bypass predecessor-worklist construction. Misses reuse a vector owned
by the current SSA pass; it is cleared between reads and released when the pass ends. The
explicit traversal does not re-enter `ReadVariable`. This narrow worklist-reuse idea is adapted
from [Eglantine PR #503](https://github.com/Djihads80/Eglantine/pull/503); its other SSA changes
are not included. The definition-normalization fix is separate from that proposal.

## Controlled compiler comparison

Measured on 6 November 2026, Windows x64, Ryzen 7 7800X3D, 32 GiB RAM, Clang 23.1.5/LLD,
Release `-O3`. Both executables use the same offline profile and diagnostic tool. The control
links the unchanged SSA implementation from `76fb0a6b`; the candidate changes only that SSA
implementation. The control source is also preserved in the
[tooling-only commit](https://github.com/monarchy/Lichii-performance/commit/54263e35f0913d293f35eecb9b33a498403c37ea).

The private corpus comes from a closed Ocarina v1.1.1 environment cache: 2,305 pipelines,
4,560 full environment/key contexts. The timing subset contains 52 contexts, ranked by prior
live translation time and code size, with at most three instances of each shader hash and
additional missing-stage coverage. It favors expensive shaders and is not a random sample.

Each context runs A/B/B/A in separate processes, 13 fresh compilations per block: 26 samples
per variant/context, 2,704 compilations in total. Every iteration copies the environment and
creates fresh IR pools. The table sums each context's median CFG-through-SPIR-V time, rather
than timing one frame or one concurrent pipeline burst.

| Stage | Contexts | Control, ms | Candidate, ms | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Vertex | 16 | 218.8108 | 51.1226 | 76.64% |
| Fragment | 24 | 312.8804 | 299.1160 | 4.40% |
| Mesh | 8 | 109.1792 | 104.5448 | 4.24% |
| Task | 2 | 72.9657 | 72.1307 | 1.14% |
| Compute | 2 | 42.4471 | 39.6677 | 6.55% |
| Selected total | 52 | 756.2831 | 566.5817 | 25.08% |

See the [per-stage/pass CSV](shader-compile-stage-results.csv) and
[anonymous per-context medians](shader-compile-case-results.csv). Wall times include background
game/OS scheduling; both compiler children use BelowNormal priority. Small differences should
not be treated as established gains. The large vertex effect is also present in a separate
normalization-only comparison (77.46% lower CFG-through-SPIR-V time).

Input reads, native lowering, environment copying, pool destruction, output writes and Vulkan
driver compilation are excluded. The operating-system and CPU caches are not flushed. These
are fresh IR compilations, not cold-system or in-game FPS measurements.

## Correctness and limits

- Six SSA cases pass: local overwrite/undefined values across eight variable kinds, branch
  Phi operands, incomplete loop Phi, a 2,048-copy chain, late trivial/nontrivial Phi resolution,
  and repeated deep predecessor traversals. They contain 4,227 assertions.
- The copy-chain check measures 2,100,225 total Identity hops / longest chain 2,049 with the
  original SSA implementation, versus 2,049 / 1 after normalization. DCE retains 2,051 versus
  3 instructions respectively, with the same final source value and live-use preservation.
  The old implementation intentionally fails these new structural bounds, not value checks.
- All 4,560 captured environment/key contexts compile successfully to byte-identical SPIR-V
  before/after: 1,735 vertex, 2,066 fragment, 239 compute, 189 task and 331 mesh. All 1,971
  distinct output modules pass `spirv-val --target-env vulkan1.2`, with no validation errors.
- All 796 C++ cases pass: 781 main, 8 update-checker, 7 configuration. The six SSA cases are
  included in the 781, not added a second time. Also pass: 30 CLI tests, 6 parser tests and
  4 existing captured regression fixtures with `spirv-val`.

Task/Mesh validation is isolated-stage validation under a portable SPIR-V 1.4/Vulkan 1.2
profile, without sibling payload/output linkage or device feature probing. Byte equality
under this profile does not prove all device profiles, shader inputs, bindings or pixels.
The existing recursive `Value::Resolve` helper is unchanged; this is not malformed-IR hardening.

Earlier live traces included a 261.344 ms NVIDIA driver call during a 307.489 ms guest interval.
This change targets redundant CPU compiler work. It does not establish that the driver stall
or all first-encounter freezes are gone. No new in-game FPS or frame-time improvement is claimed.
Ordinary play keeps profiling off. Raw game captures, caches, keys, SPIR-V and logs stay local.

To reproduce the method on your own captures, use the
[offline replay/timing instructions](https://github.com/monarchy/Lichii-performance/blob/54263e35f0913d293f35eecb9b33a498403c37ea/tools/nx2/shader-sweep.md) with the tooling-only
control and the SSA fix built using the same toolchain. Retain the full environment and key
on both sides; do not use shader-hash-only reuse as an equivalence test.

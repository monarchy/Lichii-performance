// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/object_pool.h"
#include "shader_recompiler/runtime_info.h"

namespace Shader {
struct HostTranslateInfo;
}

namespace Shader::Maxwell {

enum class TranslationPass : size_t {
    HostSetup,
    ASL,
    Blocks,
    PostOrder,
    ProgramMetadata,
    RemoveUnreachable,
    LowerFp64,
    LowerFp16,
    LowerInt64,
    ConditionalBarrier,
    SSA,
    ConstantPropagation,
    Position,
    BlitConstants,
    GlobalMemory,
    GeometryCompaction,
    Texture,
    Rescaling,
    DeadCodeElimination,
    TightenOutputVertices,
    Verification,
    CollectShaderInfo,
    MeshInfo,
    Layer,
    VendorWorkaround,
    InterpolationInfo,
    NVNStorageBuffers,
    LimitSampledTextureArrays,
    Count,
};

// Per-call wall times; skipped passes remain zero. No clock is read unless requested.
struct TranslationTimings {
    static constexpr std::array<std::string_view, static_cast<size_t>(TranslationPass::Count)> Names{
        "host_setup", "asl", "blocks", "post_order", "program_metadata", "remove_unreachable",
        "lower_fp64", "lower_fp16", "lower_int64", "conditional_barrier", "ssa",
        "constant_propagation", "position", "blit_constants", "global_memory",
        "geometry_compaction", "texture", "rescaling", "dead_code_elimination",
        "tighten_output_vertices", "verification", "collect_shader_info", "mesh_info", "layer",
        "vendor_workaround", "interpolation_info", "nvn_storage_buffers",
        "limit_sampled_texture_arrays"};
    std::array<double, Names.size()> milliseconds{};
    double total_ms{};
};

[[nodiscard]] IR::Program TranslateProgram(ObjectPool<IR::Inst>& inst_pool,
                                           ObjectPool<IR::Block>& block_pool, Environment& env,
                                           Flow::CFG& cfg, const HostTranslateInfo& host_info,
                                           TranslationTimings* timings = nullptr);

[[nodiscard]] IR::Program MergeDualVertexPrograms(IR::Program& vertex_a, IR::Program& vertex_b,
                                                  Environment& env_vertex_b);

void ConvertLegacyToGeneric(IR::Program& program, const RuntimeInfo& runtime_info);

// Maxwell v1 and older Nvidia cards don't support setting gl_Layer from non-geometry stages.
// This creates a workaround by setting the layer as a generic output and creating a
// passthrough geometry shader that reads the generic and sets the layer.
[[nodiscard]] IR::Program GenerateGeometryPassthrough(ObjectPool<IR::Inst>& inst_pool,
                                                      ObjectPool<IR::Block>& block_pool,
                                                      const HostTranslateInfo& host_info,
                                                      IR::Program& source_program,
                                                      Shader::OutputTopology output_topology);

} // namespace Shader::Maxwell

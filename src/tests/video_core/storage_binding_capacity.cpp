// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <limits>
#include <tuple>
#include <utility>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/exception.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "video_core/buffer_cache/buffer_cache_base.h"

TEST_CASE("Buffer cache holds every shader storage binding", "[video_core][storage_bindings]") {
    using Channel = VideoCommon::BufferCacheChannelInfo;
    using ComputeBindings = decltype(std::declval<Channel>().compute_storage_buffers);
    using GraphicsBindings = decltype(std::declval<Channel>().storage_buffers)::value_type;
    using ComputeMask = decltype(Channel::enabled_compute_storage_buffers);
    using GraphicsMask = decltype(Channel::enabled_storage_buffers)::value_type;
    using WrittenComputeMask = decltype(Channel::written_compute_storage_buffers);
    using WrittenGraphicsMask = decltype(Channel::written_storage_buffers)::value_type;
    STATIC_REQUIRE(std::tuple_size_v<ComputeBindings> == Shader::Info::MAX_SSBOS);
    STATIC_REQUIRE(std::tuple_size_v<GraphicsBindings> == Shader::Info::MAX_GRAPHICS_SSBOS);
    STATIC_REQUIRE(std::numeric_limits<ComputeMask>::digits == std::tuple_size_v<ComputeBindings>);
    STATIC_REQUIRE(std::numeric_limits<WrittenComputeMask>::digits == std::tuple_size_v<ComputeBindings>);
    STATIC_REQUIRE(std::numeric_limits<GraphicsMask>::digits == std::tuple_size_v<GraphicsBindings>);
    STATIC_REQUIRE(std::numeric_limits<WrittenGraphicsMask>::digits == std::tuple_size_v<GraphicsBindings>);

    const auto exercise = []<typename Bindings, typename Mask>(Bindings& slots, Mask enabled) {
        for (size_t index = 0; index < slots.size(); ++index) {
            slots.at(index).size = static_cast<u32>(index + 1);
            enabled |= Mask{1} << index;
        }
        size_t count{};
        while (enabled != 0) {
            const auto index = static_cast<size_t>(std::countr_zero(enabled));
            REQUIRE(slots.at(index).size == index + 1);
            enabled &= enabled - 1;
            ++count;
        }
        REQUIRE(count == slots.size());
    };
    ComputeBindings compute{};
    GraphicsBindings graphics{};
    exercise(compute, ComputeMask{});
    exercise(graphics, GraphicsMask{});
    // Bananza uses slots 16 and 17. Native compute pointer tables can fill slot 63.
    CHECK(compute.at(16).size == 17);
    CHECK(compute.at(17).size == 18);
    CHECK(compute.at(63).size == 64);
    CHECK(graphics.at(31).size == 32);
}

TEST_CASE("Merged vertex storage descriptors respect the graphics binding limit",
          "[shader][video_core][storage_bindings]") {
    Shader::Info vertex_a{}, vertex_b{};
    auto& bindings = vertex_a.storage_buffers_descriptors;
    for (u32 index = 0; index < Shader::Info::MAX_GRAPHICS_SSBOS; ++index) {
        bindings.push_back({0, index * 8, 1, false});
    }
    vertex_b.storage_buffers_descriptors.push_back(bindings.back());
    vertex_b.storage_buffers_descriptors.back().is_written = true;
    Shader::Optimization::JoinStorageInfo(vertex_a, vertex_b);
    REQUIRE(bindings.size() == Shader::Info::MAX_GRAPHICS_SSBOS);
    REQUIRE(bindings.back().is_written);
    vertex_b.storage_buffers_descriptors.back().cbuf_index = 1;
    REQUIRE_THROWS_AS(Shader::Optimization::JoinStorageInfo(vertex_a, vertex_b),
                      Shader::NotImplementedException);
    REQUIRE(bindings.size() == Shader::Info::MAX_GRAPHICS_SSBOS);
}

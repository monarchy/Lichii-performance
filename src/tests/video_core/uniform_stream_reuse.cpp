// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "video_core/buffer_cache/uniform_stream_reuse.h"

namespace {
void Check(bool condition, const char* message) {
    INFO(message);
    REQUIRE(condition);
}

struct Memory {
    std::array<u8, 8192> bytes{};
    bool contiguous{true};
    u32 reads{};
    u32 span_requests{};
    const u8* GetSpan(u64 address, u32 size) {
        Check(address + size <= bytes.size(), "span outside guest memory");
        ++span_requests;
        return contiguous ? bytes.data() + address : nullptr;
    }
    void ReadBlockUnsafe(u64 address, void* destination, u32 size) {
        Check(address + size <= bytes.size(), "read outside guest memory");
        ++reads;
        std::memcpy(destination, bytes.data() + address, size);
    }
};

struct Runtime {
    struct StreamUniformRef {
        size_t allocation;
    };
    std::vector<std::vector<u8>> allocations;
    StreamUniformRef last{}, bound{};
    u64 tick{1};
    Memory* mutate_on_allocation{};
    u64 CurrentTick() const {
        return tick;
    }
    StreamUniformRef LastStreamUniformRef() const {
        return last;
    }
    std::span<u8> BindMappedUniformBuffer(size_t, u32, u32 size) {
        if (mutate_on_allocation) {
            mutate_on_allocation->bytes[7] ^= u8{0x5a};
        }
        allocations.emplace_back(size);
        last = bound = StreamUniformRef{allocations.size() - 1};
        return allocations.back();
    }
    void RebindStreamUniform(StreamUniformRef ref, u32 size) {
        Check(ref.allocation < allocations.size(), "reused a recycled allocation");
        Check(allocations[ref.allocation].size() == size, "reused wrong allocation size");
        bound = ref;
    }
    void NextTick() {
        ++tick;
        allocations.clear();
    }
};

void CheckBound(const Runtime& runtime, const Memory& memory, u64 address, u32 size) {
    const auto& bytes = runtime.allocations.at(runtime.bound.allocation);
    Check(bytes.size() == size &&
              std::memcmp(bytes.data(), memory.bytes.data() + address, size) == 0,
          "bound uniform data differs from guest memory");
}
} // namespace

TEST_CASE("Uniform stream reuse preserves data through mutation and session resets",
          "[video_core][uniform_stream_reuse]") {
    using VideoCommon::BindSmallUniformReusing;
    bool direct = GENERATE(false, true);
    Memory memory;
    Runtime runtime;
    VideoCommon::ResetUniformStreamReuse<Runtime>();
    auto bind = [&](size_t stage, u32 slot, u64 address, u32 size) {
        Check(BindSmallUniformReusing(runtime, memory, stage, slot, slot, address, size, direct),
              "valid small uniform rejected");
        CheckBound(runtime, memory, address, size);
    };
    bind(0, 0, 0, 64);
    bind(0, 0, 0, 64);
    Check(runtime.allocations.size() == 1, "identical bytes were copied twice");
    memory.bytes[7] = 23;
    bind(0, 0, 0, 64);
    Check(runtime.allocations.size() == 2, "changed bytes reused stale data");
    bind(0, 1, 0, 64);
    bind(1, 0, 0, 64);
    Check(runtime.allocations.size() == 4, "stage or slot shared the wrong allocation");
    runtime.NextTick();
    bind(0, 0, 0, 64);
    Check(runtime.allocations.size() == 1, "a new command buffer reused an old allocation");
    VideoCommon::ResetUniformStreamReuse<Runtime>();
    bind(0, 0, 0, 64);
    Check(runtime.allocations.size() == 2, "session reset retained an old allocation");
    bind(4, 17, 0, 4096);
    const auto before = runtime.allocations.size();
    for (auto [stage, slot, size] :
         {std::array<u32, 3>{5, 0, 64}, {0, 18, 64}, {0, 0, 0}, {0, 0, 4097}}) {
        Check(!BindSmallUniformReusing(runtime, memory, stage, slot, slot, 0, size, direct),
              "invalid uniform range did not use the fallback");
    }
    Check(runtime.allocations.size() == before, "fallback allocated stream memory");

    u32 random = 17;
    for (u32 index = 0; index < 10000; ++index) {
        random = random * 1664525U + 1013904223U;
        if (index % 37 == 0)
            runtime.NextTick();
        if (index % 503 == 0)
            VideoCommon::ResetUniformStreamReuse<Runtime>();
        const u32 size = 1 + random % 4096;
        const u64 address = (random >> 12) % (memory.bytes.size() - 4096);
        if (index % 3 == 0)
            memory.bytes[address + random % size] ^= u8{0x5a};
        const size_t stage = (random >> 20) % 5;
        const u32 slot = (random >> 24) % 18;
        bind(stage, slot, address, size);
        direct = !direct;
        bind(stage, slot, address, size);
    }
}

TEST_CASE("Direct uniform comparison falls back for fragmented memory and keeps one snapshot",
          "[video_core][uniform_stream_direct]") {
    using VideoCommon::BindSmallUniformReusing;
    Memory memory;
    Runtime runtime;
    VideoCommon::ResetUniformStreamReuse<Runtime>();

    REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, true));
    REQUIRE(memory.reads == 0);
    CheckBound(runtime, memory, 0, 64);
    REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, true));
    REQUIRE(runtime.allocations.size() == 1);
    memory.contiguous = false;
    memory.bytes[7] = 23;
    REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, true));
    REQUIRE(memory.reads == 1);
    CheckBound(runtime, memory, 0, 64);

    for (const bool direct : {false, true}) {
        CAPTURE(direct);
        VideoCommon::ResetUniformStreamReuse<Runtime>();
        memory.contiguous = true;
        memory.bytes[7] = 23;
        runtime.mutate_on_allocation = &memory;
        REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, direct));
        runtime.mutate_on_allocation = nullptr;
        REQUIRE(runtime.allocations.back()[7] == 23);
        REQUIRE(memory.bytes[7] != 23);
        const auto before = runtime.allocations.size();
        REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, direct));
        REQUIRE(runtime.allocations.size() == before + 1);
        CheckBound(runtime, memory, 0, 64);
        runtime.NextTick();
        REQUIRE(BindSmallUniformReusing(runtime, memory, 0, 0, 0, 0, 64, direct));
        CheckBound(runtime, memory, 0, 64);
    }
}

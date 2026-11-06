// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/pipeline_transition_cache.h"

namespace {
struct Key {
    std::array<unsigned, 6> shader_hashes{};
    unsigned state{};
    bool operator==(const Key&) const = default;
};

struct Pipeline {
    const Key key;
};

const Key& KeyOf(const Pipeline* pipeline) {
    return pipeline->key;
}
} // namespace

TEST_CASE("Pipeline successors use full key equality", "[vulkan][pipeline_transitions]") {
    Vulkan::PipelineTransitionCache<Pipeline> cache;
    Pipeline first{{.state = 1}};
    Pipeline second{{.state = 2}};
    cache.Add(&first);
    cache.Add(&second);

    REQUIRE(cache.Find(first.key, KeyOf) == &first);
    REQUIRE(cache.Find(second.key, KeyOf) == &second);
    REQUIRE(cache.Find(Key{.state = 3}, KeyOf) == nullptr);
    Key other_shader = first.key;
    other_shader.shader_hashes.back() = 1;
    REQUIRE(cache.Find(other_shader, KeyOf) == nullptr);
}

TEST_CASE("Pipeline successor lookup stays bounded across changing working sets",
          "[vulkan][pipeline_transitions]") {
    constexpr std::size_t capacity = 16;
    Vulkan::PipelineTransitionCache<Pipeline, capacity> cache;
    std::vector<std::unique_ptr<Pipeline>> pipelines;
    for (unsigned i = 0; i < 257; ++i) {
        pipelines.push_back(std::make_unique<Pipeline>(Key{.state = i}));
    }
    std::mt19937 random{0x5eed};
    for (unsigned i = 0; i < 4096; ++i) {
        if (i % 113 == 0) {
            cache.Clear();
        }
        const Key requested{.state = static_cast<unsigned>(random() % pipelines.size())};
        std::size_t comparisons = 0;
        Pipeline* found = cache.Find(requested, [&](const Pipeline* pipeline) -> const Key& {
            ++comparisons;
            return pipeline->key;
        });
        REQUIRE(comparisons <= capacity);
        // The authoritative cache owns every pipeline regardless of successor retention.
        Pipeline* expected = pipelines[requested.state].get();
        if (!found) {
            found = expected;
            cache.Add(found);
        }
        REQUIRE(found == expected);
        REQUIRE(cache.Find(requested, KeyOf) == expected);
    }
}

TEST_CASE("Clearing pipeline successors prevents reads of evicted pipelines",
          "[vulkan][pipeline_transitions]") {
    Vulkan::PipelineTransitionCache<Pipeline> cache;
    auto evicted = std::make_unique<Pipeline>(Key{.state = 1});
    cache.Add(evicted.get());
    cache.Clear();
    evicted.reset();

    unsigned reads = 0;
    const Key requested{.state = 1};
    REQUIRE(cache.Find(requested, [&](const Pipeline*) -> const Key& {
                ++reads;
                return requested;
            }) == nullptr);
    REQUIRE(reads == 0);

    Pipeline replacement{requested};
    cache.Add(&replacement);
    REQUIRE(cache.Find(requested, KeyOf) == &replacement);

    // After clearing a full cache, the complete capacity is available again.
    std::vector<std::unique_ptr<Pipeline>> fresh;
    for (unsigned i = 0; i < 32; ++i) {
        fresh.push_back(std::make_unique<Pipeline>(Key{.state = i}));
        cache.Add(fresh.back().get());
    }
    cache.Clear();
    for (unsigned i = 0; i < 16; ++i) {
        cache.Add(fresh[i].get());
    }
    for (unsigned i = 0; i < 16; ++i) {
        REQUIRE(cache.Find(fresh[i]->key, KeyOf) == fresh[i].get());
    }
}

TEST_CASE("Successor overflow keeps common alternatives instead of evicting every entry",
          "[vulkan][pipeline_transitions]") {
    constexpr std::size_t capacity = 16;
    Vulkan::PipelineTransitionCache<Pipeline, capacity> cache;
    std::vector<std::unique_ptr<Pipeline>> pipelines;
    for (unsigned i = 0; i <= capacity; ++i) {
        pipelines.push_back(std::make_unique<Pipeline>(Key{.state = i}));
        cache.Add(pipelines.back().get());
    }
    unsigned hits = 0;
    for (unsigned round = 0; round < 8; ++round) {
        for (auto& pipeline : pipelines) {
            if (cache.Find(pipeline->key, KeyOf)) {
                ++hits;
            } else {
                cache.Add(pipeline.get());
            }
        }
    }
    REQUIRE(hits >= 8 * (capacity - 1));
}

TEST_CASE("Repeated successor additions remain safe and bounded",
          "[vulkan][pipeline_transitions]") {
    Vulkan::PipelineTransitionCache<Pipeline> cache;
    Pipeline first{{.state = 1}};
    Pipeline second{{.state = 2}};
    for (unsigned i = 0; i < 64; ++i) {
        cache.Add(&first);
        cache.Add(&second);
    }
    REQUIRE(cache.Find(first.key, KeyOf) == &first);
    REQUIRE(cache.Find(second.key, KeyOf) == &second);
    unsigned reads = 0;
    REQUIRE(cache.Find(Key{.state = 3}, [&](const Pipeline* pipeline) -> const Key& {
                ++reads;
                return pipeline->key;
            }) == nullptr);
    REQUIRE(reads <= 16);
}

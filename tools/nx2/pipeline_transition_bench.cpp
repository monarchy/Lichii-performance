// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Standalone synthetic benchmark, not an emulator or game benchmark. The 1024-byte
// key models six shader hashes, a compared state prefix and an unused tail; it is
// not GraphicsPipelineCacheKey's measured size. Hashing and the fallback container
// use production CityHash64 and Common::unordered_map. Driver work, synchronization,
// and the rest of CurrentGraphicsPipelineSlowPath are intentionally not modeled.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "common/cityhash.h"
#include "common/container/unordered_map.h"
#include "video_core/renderer_vulkan/pipeline_transition_cache.h"

namespace {
struct Key {
    std::array<std::uint64_t, 6> shaders{};
    std::array<std::uint64_t, 10> state{};
    std::array<std::uint64_t, 112> unused_tail{};

    bool operator==(const Key& other) const noexcept {
        return shaders == other.shaders && state == other.state;
    }
};

struct Hash {
    std::size_t operator()(const Key& key) const noexcept {
        return Common::CityHash64(reinterpret_cast<const char*>(&key),
                                  sizeof(key.shaders) + sizeof(key.state));
    }
};

struct Pipeline {
    Key key;
};

struct UnboundedTransitions {
    std::vector<Key> keys;
    std::vector<Pipeline*> pointers;

    Pipeline* Find(const Key& key, auto) const {
        const auto it = std::find(keys.begin(), keys.end(), key);
        return it == keys.end() ? nullptr : pointers[it - keys.begin()];
    }

    void Add(Pipeline* pipeline) {
        keys.push_back(pipeline->key);
        pointers.push_back(pipeline);
    }
};

using BoundedTransitions = Vulkan::PipelineTransitionCache<Pipeline>;
volatile std::uint64_t result_sink;

template <typename Cache>
double Run(std::size_t successors, bool same_shaders, bool late_hot) {
    std::vector<std::unique_ptr<Pipeline>> values;
    Common::unordered_map<Key, Pipeline*, Hash> authoritative;
    Cache cache;
    for (std::size_t i = 0; i < successors; ++i) {
        auto pipeline = std::make_unique<Pipeline>();
        if (same_shaders) {
            pipeline->key.state.back() = i;
        } else {
            pipeline->key.shaders.front() = i;
        }
        authoritative.emplace(pipeline->key, pipeline.get());
        cache.Add(pipeline.get());
        values.push_back(std::move(pipeline));
    }
    constexpr unsigned iterations = 10000;
    std::uint64_t total = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        const auto index = late_hot ? successors - 1 : i % successors;
        const Key& key = values[index]->key;
        Pipeline* pipeline = cache.Find(key, [](Pipeline* value) -> const Key& {
            return value->key;
        });
        if (!pipeline) {
            pipeline = authoritative.at(key);
            cache.Add(pipeline);
        }
        total += pipeline->key.state.back() + pipeline->key.shaders.front();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    result_sink = total;
    return std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
}

void PrintSamples(std::array<double, 5> samples) {
    std::sort(samples.begin(), samples.end());
    std::cout << samples[2] << ',' << samples.front() << ',' << samples.back();
}
} // namespace

int main() {
    std::cerr << "synthetic_key_bytes=" << sizeof(Key)
              << ",bounded_cache_bytes=" << sizeof(BoundedTransitions) << '\n';
    std::cout << "successors,same_shaders,late_hot,old_median_ns,old_min_ns,old_max_ns,"
                 "bounded_median_ns,bounded_min_ns,bounded_max_ns\n";
    for (const unsigned size : {1, 2, 8, 16, 17, 64, 256}) {
        for (const bool same_shaders : {false, true}) {
            for (const bool late_hot : {false, true}) {
                std::array<double, 5> old_samples;
                std::array<double, 5> bounded_samples;
                for (unsigned repeat = 0; repeat < old_samples.size(); ++repeat) {
                    // Alternate measurement order to reduce systematic warmup bias.
                    if (repeat % 2 == 0) {
                        old_samples[repeat] = Run<UnboundedTransitions>(size, same_shaders, late_hot);
                        bounded_samples[repeat] = Run<BoundedTransitions>(size, same_shaders, late_hot);
                    } else {
                        bounded_samples[repeat] = Run<BoundedTransitions>(size, same_shaders, late_hot);
                        old_samples[repeat] = Run<UnboundedTransitions>(size, same_shaders, late_hot);
                    }
                }
                std::cout << size << ',' << same_shaders << ',' << late_hot << ',';
                PrintSamples(old_samples);
                std::cout << ',';
                PrintSamples(bounded_samples);
                std::cout << '\n';
            }
        }
    }
}

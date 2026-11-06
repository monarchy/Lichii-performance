// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>

namespace Vulkan {

// A missed successor is still resolved by the owning pipeline cache.
template <typename Pipeline, std::size_t Capacity = 16>
class PipelineTransitionCache {
    static_assert(Capacity != 0);

public:
    template <typename Key, typename KeyOf>
    [[nodiscard]] Pipeline* Find(const Key& key, KeyOf key_of) const noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (key_of(entries[i]) == key) {
                return entries[i];
            }
        }
        return nullptr;
    }

    // Called after Find misses; ownership and key identity stay in the pipeline cache.
    void Add(Pipeline* pipeline) noexcept {
        if (count < Capacity) {
            entries[count++] = pipeline;
        } else {
            // Keep common alternatives when a draw sequence exceeds the cache capacity;
            // replacing every entry in a ring would make such a sequence miss every time.
            entries.back() = pipeline;
        }
    }

    void Clear() noexcept {
        count = 0;
    }

private:
    std::array<Pipeline*, Capacity> entries{};
    std::size_t count{};
};

} // namespace Vulkan

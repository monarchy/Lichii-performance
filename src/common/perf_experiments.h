// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>

#include "common/common_types.h"

namespace Common::Nx2Perf {

inline constexpr u32 MacroEntryWait = 1U << 0;
inline constexpr u32 UniformDirect = 1U << 1;
inline constexpr u32 FrontBatch = 1U << 2;
inline constexpr u32 KernelTimedWait = 1U << 3;

// These flags select equivalent algorithms and publish no payload or ownership.
// One shared instance permits live A/B in a fixed camera view through the opt-in debug server.
inline std::atomic<u32>& ExperimentFlags() {
    static std::atomic<u32> flags{[] {
        const auto enabled = [](const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr && value[0] == '1';
        };
        return (enabled("EDEN_NX2_MME_ENTRY_WAIT") ? MacroEntryWait : 0U) |
               (enabled("EDEN_NX2_UBO_DIRECT") ? UniformDirect : 0U) |
               (enabled("EDEN_NX2_FRONT_BATCH") ? FrontBatch : 0U) |
               (enabled("EDEN_NX2_KERNEL_TIMED_WAIT") ? KernelTimedWait : 0U);
    }()};
    return flags;
}

inline bool ExperimentEnabled(u32 flag) {
    return (ExperimentFlags().load(std::memory_order_relaxed) & flag) != 0;
}

} // namespace Common::Nx2Perf

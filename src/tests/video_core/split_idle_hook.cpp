// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>

#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "tests/core/system_test_context.h"
#include "video_core/gpu_split.h"
#include "video_core/nx2.h"

namespace {
using namespace std::chrono_literals;

struct IdleHookState {
    std::promise<void> entered;
    std::shared_future<void> entered_signal{entered.get_future().share()};
    std::promise<void> release;
    std::shared_future<void> release_signal{release.get_future().share()};
    std::promise<bool> started;
    std::promise<void> drain_started;
    std::promise<bool> drain_finished;
    std::atomic<bool> released{};
    std::atomic<unsigned> hook_calls{};
    std::atomic<unsigned> commands{};
    std::atomic<bool> hook_owns_scheduler{};
    std::atomic<bool> command_owns_scheduler{};
    std::atomic<bool> front_owns_scheduler{true};

    void Release() {
        if (!released.exchange(true)) {
            release.set_value();
        }
    }

    static void Hook(void* argument) {
        auto& state = *static_cast<IdleHookState*>(argument);
        state.hook_owns_scheduler.store(VideoCommon::GpuSplit::OnBackThread() &&
                                        VideoCommon::GpuSplit::OwnsScheduler());
        if (state.hook_calls.fetch_add(1) == 0) {
            state.entered.set_value();
        }
        state.release_signal.wait();
    }
};
} // namespace

TEST_CASE("Split Drain waits for its idle hook and survives session restart",
          "[video_core][gpu_split_idle_hook]") {
    if (const char* mode = std::getenv("EDEN_NX2_SPLIT");
        mode != nullptr && mode[0] != '1' && mode[0] != '2') {
        SKIP("EDEN_NX2_SPLIT explicitly disables the real split worker");
    }

    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    auto& split = VideoCommon::GpuSplit::Get();
    REQUIRE_FALSE(split.Active());
    REQUIRE_FALSE(split.BackRunning());

    const auto previous_accuracy = Settings::values.current_gpu_accuracy;
    const auto previous_dma = Settings::values.dma_accuracy.GetValue();
    const auto previous_renderer = Settings::values.renderer_backend.GetValue();
    const bool previous_native = Tegra::g_nx2_ampere.exchange(true);
    const bool previous_check_owner = VideoCommon::GpuSplit::check_owner;
    SCOPE_EXIT {
        Settings::values.current_gpu_accuracy = previous_accuracy;
        Settings::values.dma_accuracy.SetValue(previous_dma);
        Settings::values.renderer_backend.SetValue(previous_renderer);
        Tegra::g_nx2_ampere.store(previous_native);
        VideoCommon::GpuSplit::check_owner = previous_check_owner;
    };
    Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
    Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Default);
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);

    for (unsigned session = 0; session < 2; ++session) {
        CAPTURE(session);
        IdleHookState state;
        auto started = state.started.get_future();
        auto drain_started = state.drain_started.get_future();
        auto drain_finished = state.drain_finished.get_future();
        auto front = std::async(std::launch::async, [&] {
            VideoCommon::GpuSplit::MarkGpuThread();
            split.SetIdleHook(&IdleHookState::Hook, &state);
            SCOPE_EXIT {
                split.Stop();
                split.SetIdleHook(nullptr, nullptr);
            };
            split.MaybeStart(system);
            state.started.set_value(split.Active());
            if (!split.Active()) {
                return false;
            }
            split.Defer([&state] {
                state.command_owns_scheduler.store(VideoCommon::GpuSplit::OnBackThread() &&
                                                   VideoCommon::GpuSplit::OwnsScheduler());
                state.commands.fetch_add(1);
            });
            split.Publish();
            if (state.entered_signal.wait_for(5s) != std::future_status::ready) {
                return false;
            }
            state.front_owns_scheduler.store(VideoCommon::GpuSplit::OwnsScheduler());
            state.drain_started.set_value();
            split.Drain();
            // Signal before Stop: Stop itself waits for the blocked hook, which would hide
            // a Drain that returned after consumption but before the hook completed.
            state.drain_finished.set_value(!state.released.load());
            return true;
        });
        // On an assertion failure, unblock the hook before the async front joins.
        SCOPE_EXIT { state.Release(); };

        REQUIRE(started.wait_for(5s) == std::future_status::ready);
        REQUIRE(started.get());
        REQUIRE(state.entered_signal.wait_for(5s) == std::future_status::ready);
        REQUIRE(drain_started.wait_for(5s) == std::future_status::ready);
        CHECK(state.commands.load() == 1);
        CHECK(state.command_owns_scheduler.load());
        CHECK(state.hook_owns_scheduler.load());
        CHECK_FALSE(state.front_owns_scheduler.load());
        CHECK(drain_finished.wait_for(50ms) == std::future_status::timeout);

        state.Release();
        REQUIRE(drain_finished.wait_for(5s) == std::future_status::ready);
        CHECK_FALSE(drain_finished.get());
        REQUIRE(front.get());
        CHECK_FALSE(split.Active());
        CHECK_FALSE(split.BackRunning());
    }
}

TEST_CASE("Split entry wait preserves command completion without waiting for the idle hook",
          "[video_core][gpu_split_idle_hook]") {
    if (const char* mode = std::getenv("EDEN_NX2_SPLIT");
        mode != nullptr && mode[0] != '1' && mode[0] != '2') {
        SKIP("EDEN_NX2_SPLIT explicitly disables the real split worker");
    }

    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    auto& split = VideoCommon::GpuSplit::Get();
    REQUIRE_FALSE(split.Active());
    REQUIRE_FALSE(split.BackRunning());

    const auto previous_accuracy = Settings::values.current_gpu_accuracy;
    const auto previous_dma = Settings::values.dma_accuracy.GetValue();
    const auto previous_renderer = Settings::values.renderer_backend.GetValue();
    const bool previous_native = Tegra::g_nx2_ampere.exchange(true);
    const bool previous_check_owner = VideoCommon::GpuSplit::check_owner;
    SCOPE_EXIT {
        Settings::values.current_gpu_accuracy = previous_accuracy;
        Settings::values.dma_accuracy.SetValue(previous_dma);
        Settings::values.renderer_backend.SetValue(previous_renderer);
        Tegra::g_nx2_ampere.store(previous_native);
        VideoCommon::GpuSplit::check_owner = previous_check_owner;
    };
    Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
    Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Default);
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);

    IdleHookState state;
    std::promise<void> command_entered;
    auto command_entered_signal = command_entered.get_future();
    std::promise<void> command_release;
    auto command_release_signal = command_release.get_future().share();
    std::atomic<bool> command_released{};
    const auto release_command = [&] {
        if (!command_released.exchange(true)) {
            command_release.set_value();
        }
    };
    auto started = state.started.get_future();
    auto wait_started = state.drain_started.get_future();
    auto wait_finished = state.drain_finished.get_future();
    auto front = std::async(std::launch::async, [&] {
        VideoCommon::GpuSplit::MarkGpuThread();
        split.SetIdleHook(&IdleHookState::Hook, &state);
        SCOPE_EXIT {
            split.Stop();
            split.SetIdleHook(nullptr, nullptr);
        };
        split.MaybeStart(system);
        state.started.set_value(split.Active());
        if (!split.Active()) {
            return false;
        }
        split.Defer([&state, &command_entered, &command_release_signal] {
            state.command_owns_scheduler.store(VideoCommon::GpuSplit::OnBackThread() &&
                                               VideoCommon::GpuSplit::OwnsScheduler());
            command_entered.set_value();
            command_release_signal.wait();
            state.commands.fetch_add(1);
        });
        state.drain_started.set_value();
        split.WaitForEntries(VideoCommon::DrainSite::MacroDmaRead);
        state.front_owns_scheduler.store(VideoCommon::GpuSplit::OwnsScheduler());
        state.drain_finished.set_value(!state.released.load());
        // Keep the worker alive until its hook runs; Stop could otherwise win that race.
        return state.entered_signal.wait_for(5s) == std::future_status::ready;
    });
    SCOPE_EXIT {
        release_command();
        state.Release();
    };

    REQUIRE(started.wait_for(5s) == std::future_status::ready);
    REQUIRE(started.get());
    REQUIRE(wait_started.wait_for(5s) == std::future_status::ready);
    REQUIRE(command_entered_signal.wait_for(5s) == std::future_status::ready);
    CHECK(wait_finished.wait_for(50ms) == std::future_status::timeout);
    CHECK(state.commands.load() == 0);

    release_command();
    REQUIRE(wait_finished.wait_for(5s) == std::future_status::ready);
    CHECK(wait_finished.get());
    CHECK(state.commands.load() == 1);
    CHECK(state.command_owns_scheduler.load());
    CHECK_FALSE(state.front_owns_scheduler.load());
    REQUIRE(state.entered_signal.wait_for(5s) == std::future_status::ready);
    CHECK(state.hook_owns_scheduler.load());

    state.Release();
    REQUIRE(front.get());
    CHECK_FALSE(split.Active());
    CHECK_FALSE(split.BackRunning());
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include "common/common_types.h"
#include "common/polyfill_thread.h"

namespace Common {

enum class EventWaitPolicy { Default, KernelTimer };

class Event {
public:
#ifdef _WIN32
    ~Event();
#endif
    void Set() {
        std::scoped_lock lk{mutex};
        if (!is_set) {
            is_set = true;
            condvar.notify_one();
#ifdef _WIN32
            if (kernel_waiters != 0) {
                WakeKernelWaiters();
            }
#endif
        }
    }

    void Wait() {
        std::unique_lock lk{mutex};
        condvar.wait(lk, [this] { return TryConsumeSignal(); });
    }

    bool WaitFor(std::chrono::nanoseconds time, EventWaitPolicy policy = EventWaitPolicy::Default);

    template<class Clock, class Duration>
    bool WaitUntil(const std::chrono::time_point<Clock, Duration> time) {
        std::unique_lock lk{mutex};
        return condvar.wait_until(lk, time, [this] { return TryConsumeSignal(); });
    }

    void Reset() {
        std::unique_lock lk{mutex};
        // no other action required, since wait loops on the predicate and any lingering signal will
        // get cleared on the first iteration
        is_set = false;
    }

    [[nodiscard]] bool IsSet() const {
        return is_set;
    }

private:
    // Hardware waits may consume a Set without acquiring mutex.
    bool TryConsumeSignal() {
        return is_set.load(std::memory_order_acquire) &&
               is_set.exchange(false, std::memory_order_acq_rel);
    }

    alignas(64) std::atomic<bool> is_set{false};
    std::condition_variable condvar;
    std::mutex mutex;
#ifdef _WIN32
    bool WaitForKernel(std::chrono::nanoseconds timeout);
    void WakeKernelWaiters();
    void* kernel_wake_event{}; ///< Manual-reset event, protected by mutex.
    unsigned kernel_waiters{};
#endif
};

class Barrier {
public:
    explicit Barrier(std::size_t count_) : count(count_) {}

    /// Blocks until all "count" threads have called Sync()
    bool Sync(std::stop_token token = {}) {
        std::unique_lock lk{mutex};
        const std::size_t current_generation = generation;

        if (++waiting == count) {
            generation++;
            waiting = 0;
            condvar.notify_all();
            return true;
        } else {
            condvar.wait(lk, token,
                        [this, current_generation] { return current_generation != generation; });
            return !token.stop_requested();
        }
    }

private:
    std::condition_variable_any condvar;
    std::mutex mutex;
    std::size_t count;
    std::size_t waiting = 0;
    std::size_t generation = 0; // Incremented once each time the barrier is used
};

enum class ThreadPriority : u32 {
    Low = 0,
    Normal = 1,
    High = 2,
    VeryHigh = 3,
    Critical = 4,
};

enum class ThreadPlacement : u32 {
    Default = 0,
    Background = 1,
    Efficiency = 2,
};

void SetCurrentThreadPriority(ThreadPriority new_priority);
void SetCurrentThreadName(const char* name);
void SetCurrentThreadToPerformanceCores();
void SetCurrentThreadToEfficiencyCores();
void SetCurrentThreadToBackgroundWork();
void SetCurrentThreadToAllCores();

void RefreshThreadPolicies();
void PollThreadPolicies();

} // namespace Common

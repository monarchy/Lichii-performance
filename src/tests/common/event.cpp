// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <barrier>
#include <chrono>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/event_wait.h"
#include "common/thread.h"

using namespace std::chrono_literals;

TEST_CASE("Event deadlines preserve elapsed time across wakeups", "[common][event]") {
    const auto deadline = Common::EventWaitDeadline(100ns, 2s);
    REQUIRE(deadline == 2s + 100ns);
    CHECK(Common::MwaitxTimeoutCycles(deadline - 100ns, 3'700'000'000) == 0xffffffffU);
    CHECK(Common::MwaitxTimeoutCycles(deadline - (1s + 100ns), 3'700'000'000) ==
          3'700'000'000U);
    CHECK(Common::MwaitxTimeoutCycles(deadline - (2s + 99ns), 3'700'000'000) == 4);
    CHECK(Common::MwaitxTimeoutCycles(deadline - (2s + 100ns), 3'700'000'000) == 0);
    CHECK(Common::MwaitxTimeoutCycles(deadline - (2s + 101ns), 3'700'000'000) == 0);
}

TEST_CASE("Event deadlines saturate and reject nonpositive timeouts", "[common][event]") {
    constexpr auto Max = std::chrono::nanoseconds::max();
    CHECK(Common::EventWaitDeadline(100ns, 0ns) == 100ns);
    CHECK(Common::EventWaitDeadline(100ns, -1ns) == 100ns);
    CHECK(Common::EventWaitDeadline(100ns, std::chrono::nanoseconds::min()) == 100ns);
    CHECK(Common::EventWaitDeadline(100ns, Max) == Max);
    CHECK(Common::EventWaitDeadline(Max - 1ns, 1ns) == Max);
    CHECK(Common::EventWaitDeadline(Max - 1ns, 2ns) == Max);
    CHECK(Common::MwaitxTimeoutCycles(Max, 3'700'000'000) == 0xffffffffU);
    CHECK(Common::MwaitxTimeoutCycles(1ns, 3'700'000'000) == 4);
    CHECK(Common::MwaitxTimeoutCycles(1ns, 1'000'000'000) == 1);
    CHECK(Common::MwaitxTimeoutCycles(4'294'967'295ns, 1'000'000'000) == 0xffffffffU);
    CHECK(Common::MwaitxTimeoutCycles(4'294'967'296ns, 1'000'000'000) == 0xffffffffU);
}

TEST_CASE("Event resets cannot end an unsignalled timed wait", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    std::jthread setter([&] {
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(100us);
            event.Reset();
        }
        event.Set();
    });
    CHECK(event.WaitFor(2s, policy));
    setter.join();
    CHECK_FALSE(event.IsSet());
}

TEST_CASE("Event extreme timeouts preserve signal consumption", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    CHECK_FALSE(event.WaitFor(std::chrono::nanoseconds::min(), policy));
    event.Set();
    CHECK(event.WaitFor(std::chrono::nanoseconds::min(), policy));
    CHECK_FALSE(event.IsSet());
    std::jthread setter([&] {
        std::this_thread::sleep_for(5ms);
        event.Set();
    });
    CHECK(event.WaitFor(std::chrono::nanoseconds::max(), policy));
    setter.join();
    CHECK_FALSE(event.IsSet());
}

TEST_CASE("Event timed waits consume an existing signal", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    CHECK_FALSE(event.WaitFor(0ns, policy));
    CHECK_FALSE(event.WaitFor(-1ns, policy));
    event.Set();
    CHECK(event.WaitFor(0ns, policy));
    CHECK_FALSE(event.IsSet());
    CHECK_FALSE(event.WaitFor(0ns, policy));
}

TEST_CASE("Event short timeouts do not return early", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    for (const auto timeout : {200us, 500us, 2000us}) {
        const auto start = std::chrono::steady_clock::now();
        CHECK_FALSE(event.WaitFor(timeout, policy));
        CHECK(std::chrono::steady_clock::now() - start >= timeout);
    }
}

TEST_CASE("Event timeout expiry uses the steady clock", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    for (const auto timeout : {100ms, 500ms}) {
        for (int trial = 0; trial < 2; ++trial) {
            const auto start = std::chrono::steady_clock::now();
            const bool signalled = event.WaitFor(timeout, policy);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            CAPTURE(timeout, trial, elapsed);
            CHECK_FALSE(signalled);
            CHECK(elapsed >= timeout);
        }
    }
}

TEST_CASE("Event Set interrupts a long timed wait", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    std::jthread setter([&] {
        std::this_thread::sleep_for(5ms);
        event.Set();
    });
    CHECK(event.WaitFor(2s, policy));
    setter.join();
    CHECK_FALSE(event.WaitFor(0ns, policy));
}

TEST_CASE("Event reset discards a prior timed-wait signal", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    CHECK_FALSE(event.WaitFor(100us, policy));
    event.Set();
    event.Reset();
    CHECK_FALSE(event.WaitFor(1ms, policy));
    event.Set();
    CHECK(event.WaitFor(0ns, policy));
}

TEST_CASE("Concurrent Event timed waits have independent deadlines", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    std::barrier ready(3);
    bool short_result{}, long_result{};
    std::chrono::steady_clock::duration short_elapsed{}, long_elapsed{};
    std::jthread short_wait([&] {
        ready.arrive_and_wait();
        const auto start = std::chrono::steady_clock::now();
        short_result = event.WaitFor(20ms, policy);
        short_elapsed = std::chrono::steady_clock::now() - start;
    });
    std::jthread long_wait([&] {
        ready.arrive_and_wait();
        const auto start = std::chrono::steady_clock::now();
        long_result = event.WaitFor(500ms, policy);
        long_elapsed = std::chrono::steady_clock::now() - start;
    });
    ready.arrive_and_wait();
    short_wait.join();
    long_wait.join();
    CHECK_FALSE(short_result);
    CHECK_FALSE(long_result);
    CHECK(short_elapsed >= 20ms);
    CHECK(short_elapsed < 400ms);
    CHECK(long_elapsed >= 500ms);
}

TEST_CASE("Event signalling across timed and untimed waits remains consumable", "[common][event]") {
    const auto policy = GENERATE(Common::EventWaitPolicy::Default, Common::EventWaitPolicy::KernelTimer);
    Common::Event event;
    Common::Event acknowledgement;
    std::atomic<bool> completed{true};
    std::jthread worker([&] {
        for (int i = 0; i < 100; ++i) {
            if (!event.WaitFor(2s, policy)) {
                completed = false;
                acknowledgement.Set();
                return;
            }
            acknowledgement.Set();
        }
    });
    for (int i = 0; i < 100; ++i) {
        event.Set();
        acknowledgement.Wait();
        if (!completed) {
            break;
        }
    }
    worker.join();
    CHECK(completed.load());
    CHECK_FALSE(event.WaitFor(0ns, policy));
}

#ifdef _WIN32
TEST_CASE("One signal releases one waiter across hardware and kernel policies", "[common][event]") {
    Common::Event event;
    std::barrier ready(3);
    bool default_result{}, kernel_result{};
    const auto wait = [&](Common::EventWaitPolicy policy, bool& result) {
        ready.arrive_and_wait();
        result = event.WaitFor(100ms, policy);
    };
    std::jthread first{wait, Common::EventWaitPolicy::Default, std::ref(default_result)};
    std::jthread second{wait, Common::EventWaitPolicy::KernelTimer, std::ref(kernel_result)};
    ready.arrive_and_wait();
    event.Set();
    first.join();
    second.join();
    CHECK(default_result != kernel_result);
    CHECK_FALSE(event.IsSet());
}
#endif

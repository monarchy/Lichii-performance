// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <random>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "common/slot_vector.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/buffer_cache/usage_tracker.h"
#include "video_core/renderer_vulkan/fragment_barrier_state.h"

namespace {

// Independent interval coverage: no recent-range cache or remembered full-page run.
class ReferenceUsageTracker {
    static constexpr u64 PageBytes = 4096;
    static constexpr u64 GranuleBytes = 64;

public:
    explicit ReferenceUsageTracker(size_t size) : pages(size / PageBytes + 1, 0ULL) {}

    void Reset() noexcept {
        std::ranges::fill(pages, 0ULL);
    }

    void Track(u64 offset, u64 size) noexcept {
        const u64 end = offset + size;
        if (size == 0 || end < offset || end / PageBytes >= pages.size()) {
            return;
        }
        for (u64 page = offset / PageBytes; page <= (end - 1) / PageBytes; ++page) {
            const u64 page_start = page * PageBytes;
            const u64 first = ((std::max)(offset, page_start) - page_start) / GranuleBytes;
            const u64 last = ((std::min)(end, page_start + PageBytes) - page_start - 1) / GranuleBytes;
            if (first == 0 && last == 63) {
                pages[page] = ~u64{0};
            } else {
                for (u64 bit = first; bit <= last; ++bit) {
                    pages[page] |= u64{1} << bit;
                }
            }
        }
    }

    bool IsUsed(u64 offset, u64 size) const noexcept {
        const u64 end = offset + size;
        if (size == 0 || end < offset || end / PageBytes >= pages.size()) {
            return false;
        }
        for (u64 bit = offset / GranuleBytes; bit <= (end - 1) / GranuleBytes; ++bit) {
            if ((pages[bit / 64] & (u64{1} << (bit % 64))) != 0) {
                return true;
            }
        }
        return false;
    }

    std::vector<u64> pages;
};

class FakeRasterizer {
public:
    void UpdatePagesCachedCount(DAddr addr, size_t size, s32 delta) {
        calls.emplace_back(addr, size, delta);
    }

    void UpdatePagesCachedBatch(std::span<const std::pair<DAddr, size_t>> ranges, s32 delta) {
        for (const auto& [addr, size] : ranges) {
            calls.emplace_back(addr, size, delta);
        }
    }

    std::vector<std::tuple<DAddr, u64, int>> calls;
};

using Tracker = VideoCommon::MemoryTrackerBase<FakeRasterizer>;
using Ranges = std::vector<std::pair<u64, u64>>;

constexpr u64 PAGE = 4096;
constexpr u64 WORD = PAGE * 64;
constexpr u64 MANAGER = 4ULL << 20;
constexpr DAddr BASE = 16 * MANAGER;

Ranges Upload(Tracker& tracker, DAddr addr, u64 size) {
    Ranges ranges;
    tracker.ForEachUploadRange(addr, size, [&](u64 range_addr, u64 range_size) {
        ranges.emplace_back(range_addr, range_size);
    });
    return ranges;
}

Ranges DirtyUpload(Tracker& tracker, DAddr addr, u64 size) {
    Ranges ranges;
    tracker.ForEachDirtyUploadRange(addr, size, [&](u64 range_addr, u64 range_size) {
        ranges.emplace_back(range_addr, range_size);
    });
    return ranges;
}

} // Anonymous namespace

TEST_CASE("Nx2Perf draw-binds: deferred barrier survives unrelated draws", "[video_core]") {
    Vulkan::FragmentBarrierState barrier;
    const auto prepare_draw = [&](bool stores, bool samples_attachment) {
        const bool end_pass = barrier.Pending() && (stores || samples_attachment);
        if (end_pass) {
            barrier.Reset();
        }
        return end_pass;
    };
    REQUIRE_FALSE(prepare_draw(false, true));
    barrier.Defer(true);
    REQUIRE_FALSE(prepare_draw(false, false));
    REQUIRE_FALSE(prepare_draw(false, false));
    REQUIRE(prepare_draw(false, true));
    REQUIRE_FALSE(prepare_draw(false, true));
    barrier.Defer(true);
    REQUIRE(prepare_draw(true, false));
    barrier.Defer(true);
    barrier.Reset();
    REQUIRE_FALSE(prepare_draw(true, true));
    barrier.Defer(false);
    REQUIRE_FALSE(prepare_draw(true, true));
}

TEST_CASE("Nx2Perf draw-binds: usage survives slot relocation and reset", "[video_core]") {
    constexpr u64 size = 16 * MANAGER;
    Common::SlotVector<VideoCommon::UsageTracker> slots;
    const auto id = slots.insert(size);
    slots[id].Track(0, size);
    const std::vector<u64> before{slots[id].Pages().begin(), slots[id].Pages().end()};
    for (size_t i = 0; i < 130; ++i) {
        const auto other = slots.insert(PAGE);
        slots[other].Track(64, 64);
    }
    REQUIRE(std::ranges::equal(slots[id].Pages(), before));
    slots[id].Reset();
    REQUIRE(std::ranges::all_of(slots[id].Pages(), [](u64 word) { return word == 0; }));
    ReferenceUsageTracker old{size};
    for (u64 i = 0; i < 8; ++i) {
        slots[id].Track(i * PAGE, 2 * WORD + 64);
        old.Track(i * PAGE, 2 * WORD + 64);
    }
    REQUIRE(std::ranges::equal(slots[id].Pages(), old.pages));
    auto copied = slots[id];
    VideoCommon::UsageTracker moved{PAGE};
    moved = std::move(copied);
    moved.Track(4 * WORD, WORD);
    old.Track(4 * WORD, WORD);
    REQUIRE(std::ranges::equal(moved.Pages(), old.pages));
    slots.erase(id);
    const auto recycled = slots.insert(size);
    REQUIRE(recycled == id);
    REQUIRE(std::ranges::all_of(slots[recycled].Pages(), [](u64 word) { return word == 0; }));
    slots[recycled].Track(PAGE, 3 * WORD);
    old.Reset();
    old.Track(PAGE, 3 * WORD);
    REQUIRE(std::ranges::equal(slots[recycled].Pages(), old.pages));
}

TEST_CASE("Nx2Perf draw-binds: clean words survive manager pool growth", "[video_core]") {
    FakeRasterizer full_rasterizer;
    FakeRasterizer dirty_rasterizer;
    auto full = std::make_unique<Tracker>(full_rasterizer);
    auto dirty = std::make_unique<Tracker>(dirty_rasterizer);
    for (u64 i = 0; i < 65; ++i) {
        const auto address = BASE + i * MANAGER;
        REQUIRE(Upload(*full, address, MANAGER) == DirtyUpload(*dirty, address, MANAGER));
        full->CachedCpuWrite(address + 15 * WORD + PAGE, 16);
        dirty->CachedCpuWrite(address + 15 * WORD + PAGE, 16);
    }
    full->FlushCachedWrites();
    dirty->FlushCachedWrites();
    REQUIRE(dirty->CleanWordsHold(BASE, 65 * MANAGER));
    REQUIRE(Upload(*full, BASE + 1, 65 * MANAGER - 2) ==
            DirtyUpload(*dirty, BASE + 1, 65 * MANAGER - 2));
    REQUIRE(full_rasterizer.calls == dirty_rasterizer.calls);
    REQUIRE(dirty->CleanWordsHold(BASE, 65 * MANAGER));
    REQUIRE(DirtyUpload(*dirty, BASE, 65 * MANAGER).empty());
}

TEST_CASE("Nx2Perf draw-binds: UsageTracker run matches independent byte coverage", "[video_core]") {
    constexpr u64 buffer_size = 24ULL << 20;
    std::mt19937_64 rng{0x0400486024DEC000ULL};
    VideoCommon::UsageTracker tracker{buffer_size};
    ReferenceUsageTracker old{buffer_size};
    const auto random_range = [&]() -> std::pair<u64, u64> {
        static constexpr u64 sizes[] = {16, 64, 100, 256, 4096, 6000, 65536, 1ULL << 20, 8ULL << 20};
        const u64 size = sizes[rng() % std::size(sizes)] + (rng() % 2 == 0 ? 0 : rng() % 8192);
        const u64 offset = rng() % 2 == 0 ? (rng() % (buffer_size / 4096)) * 4096 : rng() % buffer_size;
        return {offset, size};
    };
    std::vector<std::pair<u64, u64>> history;
    for (int step = 0; step < 200000; ++step) {
        const u32 choice = static_cast<u32>(rng() % 1000);
        if (choice == 0) {
            tracker.Reset();
            old.Reset();
            history.clear();
            continue;
        }
        std::pair<u64, u64> range;
        if (choice < 600 && !history.empty()) {
            range = history[rng() % history.size()];
        } else {
            range = random_range();
            history.push_back(range);
        }
        tracker.Track(range.first, range.second);
        old.Track(range.first, range.second);
        if (step % 4096 == 0) {
            REQUIRE(std::ranges::equal(tracker.Pages(), old.pages));
        }
    }
    REQUIRE(std::ranges::equal(tracker.Pages(), old.pages));
}

TEST_CASE("UsageTracker covers partial granules and refuses wrapped ranges", "[video_core][usage_tracker]") {
    constexpr u64 page = 4096;
    VideoCommon::UsageTracker tracker{2 * page};
    tracker.Track(63, 2);
    REQUIRE(tracker.Pages()[0] == 3);
    CHECK(tracker.IsUsed(0, 1));
    CHECK(tracker.IsUsed(127, 1));
    CHECK_FALSE(tracker.IsUsed(128, 1));
    tracker.Reset();
    tracker.Track(page - 1, 2);
    REQUIRE(tracker.Pages()[0] == (u64{1} << 63));
    REQUIRE(tracker.Pages()[1] == 1);
    CHECK(tracker.IsUsed(page + 63, 1));
    CHECK_FALSE(tracker.IsUsed(page + 64, 1));
    const std::vector<u64> before{tracker.Pages().begin(), tracker.Pages().end()};
    for (const auto& [offset, size] : {std::pair<u64, u64>{0, 0}, {page + 1, ~u64{0}},
                                      {~u64{0} - 31, 64}, {4 * page, 64}}) {
        CAPTURE(offset, size);
        tracker.Track(offset, size);
        CHECK(std::ranges::equal(tracker.Pages(), before));
        CHECK_FALSE(tracker.IsUsed(offset, size));
    }
    tracker.Reset();
    CHECK(std::ranges::all_of(tracker.Pages(), [](u64 bits) { return bits == 0; }));
}

TEST_CASE("UsageTracker queries match independent granule occupancy", "[video_core][usage_tracker]") {
    constexpr u64 size = 16 * 4096;
    VideoCommon::UsageTracker tracker{size};
    ReferenceUsageTracker reference{size};
    std::mt19937_64 random{0x0400486024DEC001ULL};
    for (u32 step = 0; step < 20000; ++step) {
        if (step % 89 == 0) {
            tracker.Reset();
            reference.Reset();
        }
        if (step % 3 == 0) {
            const u64 offset = random() % size;
            const u64 bytes = random() % 8193;
            tracker.Track(offset, bytes);
            reference.Track(offset, bytes);
        }
        for (u32 query = 0; query < 4; ++query) {
            const u64 offset = random() % size;
            const u64 bytes = random() % 8193;
            CAPTURE(step, offset, bytes);
            REQUIRE(tracker.IsUsed(offset, bytes) == reference.IsUsed(offset, bytes));
        }
    }
}

TEST_CASE("Nx2Perf draw-binds: clean words", "[video_core]") {
    FakeRasterizer rasterizer;
    auto tracker = std::make_unique<Tracker>(rasterizer);
    // A new manager is CPU-modified everywhere.
    REQUIRE(DirtyUpload(*tracker, BASE + 100, 3 * WORD) == Ranges{{BASE, 3 * WORD + PAGE}});
    REQUIRE(DirtyUpload(*tracker, BASE, MANAGER) == Ranges{{BASE + 3 * WORD + PAGE, MANAGER - 3 * WORD - PAGE}});
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    const size_t calls = rasterizer.calls.size();
    REQUIRE(DirtyUpload(*tracker, BASE, MANAGER).empty());
    REQUIRE(rasterizer.calls.size() == calls);

    tracker->MarkRegionAsCpuModified(BASE + 5 * WORD + 3 * PAGE, 16);
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    REQUIRE(DirtyUpload(*tracker, BASE + 5 * WORD, PAGE).empty());
    REQUIRE(DirtyUpload(*tracker, BASE, MANAGER) == Ranges{{BASE + 5 * WORD + 3 * PAGE, PAGE}});

    // A cached write is Untracked before it is flushed.
    tracker->CachedCpuWrite(BASE + 9 * WORD, 4);
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    tracker->FlushCachedWrites();
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    REQUIRE(DirtyUpload(*tracker, BASE + 9 * WORD, WORD) == Ranges{{BASE + 9 * WORD, PAGE}});

    // Unmarking makes a word clean again, as the upload would.
    tracker->MarkRegionAsCpuModified(BASE + 2 * WORD, 2 * PAGE);
    tracker->UnmarkRegionAsCpuModified(BASE + 2 * WORD, PAGE);
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    tracker->UnmarkRegionAsCpuModified(BASE + 2 * WORD + PAGE, PAGE);
    REQUIRE(tracker->CleanWordsHold(BASE, MANAGER));
    REQUIRE(DirtyUpload(*tracker, BASE, MANAGER).empty());

    // GPU writes leave words clean; a range reaching a missing manager creates it.
    tracker->MarkRegionAsGpuModified(BASE, MANAGER);
    REQUIRE(DirtyUpload(*tracker, BASE + MANAGER - WORD, 2 * WORD) == Ranges{{BASE + MANAGER, WORD}});
    REQUIRE(tracker->CleanWordsHold(BASE, 2 * MANAGER));
}

TEST_CASE("Nx2Perf draw-binds: dirty-word uploads match full uploads", "[video_core]") {
    FakeRasterizer full_rasterizer;
    FakeRasterizer narrow_rasterizer;
    auto full = std::make_unique<Tracker>(full_rasterizer);
    auto narrow = std::make_unique<Tracker>(narrow_rasterizer);
    std::mt19937_64 rng{0x8e0ULL};
    constexpr u64 span = 6 * MANAGER;
    const auto random_range = [&]() -> std::pair<DAddr, u64> {
        static constexpr u64 sizes[] = {4, 256, PAGE, 3 * PAGE, WORD, 3 * WORD + 100, MANAGER,
                                        2 * MANAGER + 5 * PAGE};
        const u64 size = sizes[rng() % std::size(sizes)];
        const u64 offset = rng() % 2 == 0 ? (rng() % (span / PAGE)) * PAGE : rng() % span;
        return {BASE + offset, (std::min)(size, span - offset)};
    };
    for (int step = 0; step < 40000; ++step) {
        const auto [addr, size] = random_range();
        switch (rng() % 10) {
        case 0:
            full->MarkRegionAsCpuModified(addr, size);
            narrow->MarkRegionAsCpuModified(addr, size);
            break;
        case 1:
            full->UnmarkRegionAsCpuModified(addr, size);
            narrow->UnmarkRegionAsCpuModified(addr, size);
            break;
        case 2:
            full->CachedCpuWrite(addr, size);
            narrow->CachedCpuWrite(addr, size);
            break;
        case 3:
            full->FlushCachedWrites();
            narrow->FlushCachedWrites();
            break;
        case 4:
            full->MarkRegionAsGpuModified(addr, size);
            narrow->MarkRegionAsGpuModified(addr, size);
            break;
        case 5:
            full->UnmarkRegionAsGpuModified(addr, size);
            narrow->UnmarkRegionAsGpuModified(addr, size);
            break;
        default:
            REQUIRE(Upload(*full, addr, size) == DirtyUpload(*narrow, addr, size));
            break;
        }
        REQUIRE(full_rasterizer.calls == narrow_rasterizer.calls);
        REQUIRE(narrow->CleanWordsHold(BASE, span));
        if (step % 1000 == 0) {
            for (u64 page = 0; page < span; page += PAGE) {
                REQUIRE(full->IsRegionCpuModified(BASE + page, PAGE) ==
                        narrow->IsRegionCpuModified(BASE + page, PAGE));
                REQUIRE(full->IsRegionGpuModified(BASE + page, PAGE) ==
                        narrow->IsRegionGpuModified(BASE + page, PAGE));
            }
        }
    }
    REQUIRE(full->ModifiedCpuRegion(BASE, span) == narrow->ModifiedCpuRegion(BASE, span));
}

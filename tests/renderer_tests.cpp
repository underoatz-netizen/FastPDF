// fastpdf_renderer_tests - PDFium-free unit tests for the Phase-3 render
// pipeline primitives: RenderKey/Quality, the byte-bounded LRU CPU cache, the
// bounded coalescing priority scheduler, and the preview/final idle state.
// No PDFium and no Windows are required.

#include <fastpdf/renderer/idle.h>
#include <fastpdf/renderer/lru_cache.h>
#include <fastpdf/renderer/priority_scheduler.h>
#include <fastpdf/renderer/render_key.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_harness.h"

namespace {

using fastpdf::renderer::Bitmap;
using fastpdf::renderer::ClampPixelSize;
using fastpdf::renderer::CpuBitmapCache;
using fastpdf::renderer::IdleDebounce;
using fastpdf::renderer::kMaxRenderDimension;
using fastpdf::renderer::PixelSize;
using fastpdf::renderer::PreviewSizeFor;
using fastpdf::renderer::PriorityScheduler;
using fastpdf::renderer::Quality;
using fastpdf::renderer::RenderKey;
using fastpdf::renderer::RenderKeyHash;
using fastpdf::renderer::RenderPriority;

// Builds a bitmap of the given dimensions whose byte size is width*4*height.
Bitmap MakeBitmap(int width, int height) {
    Bitmap b;
    b.width = width;
    b.height = height;
    b.stride = width * 4;
    b.data.resize(static_cast<std::size_t>(width) * 4 *
                  static_cast<std::size_t>(height));
    return b;
}

RenderKey Key(int page, int w, int h, Quality q = Quality::Final) {
    return RenderKey{/*docEpoch=*/1, page, w, h, /*rotation=*/0, q};
}

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// RenderKey / pixel-size math
// ---------------------------------------------------------------------------

FASTPDF_TEST(render_key_equality_and_hash) {
    const RenderKey a = Key(2, 100, 200, Quality::Preview);
    const RenderKey b = Key(2, 100, 200, Quality::Preview);
    FASTPDF_CHECK(a == b);
    FASTPDF_CHECK_EQ(RenderKeyHash{}(a), RenderKeyHash{}(b));

    const RenderKey c = Key(3, 100, 200, Quality::Preview);
    FASTPDF_CHECK(!(a == c));
}

FASTPDF_TEST(render_key_quality_distinguishes) {
    FASTPDF_CHECK(!(Key(1, 100, 100, Quality::Preview) ==
                    Key(1, 100, 100, Quality::Final)));
}

FASTPDF_TEST(clamp_pixel_size_preserves_small_sizes) {
    const PixelSize size = ClampPixelSize(100, 50);
    FASTPDF_CHECK_EQ(size.width, 100);
    FASTPDF_CHECK_EQ(size.height, 50);
}

FASTPDF_TEST(clamp_pixel_size_bounds_and_preserves_aspect) {
    const PixelSize size = ClampPixelSize(20000, 30000);
    FASTPDF_CHECK(size.width <= kMaxRenderDimension);
    FASTPDF_CHECK(size.height <= kMaxRenderDimension);
    FASTPDF_CHECK(size.width >= 1);
    FASTPDF_CHECK(size.height >= 1);
    // Aspect ratio preserved (2:3).
    FASTPDF_CHECK(Near(static_cast<double>(size.width) / size.height, 2.0 / 3.0,
                       1e-3));
}

FASTPDF_TEST(clamp_pixel_size_handles_invalid) {
    const PixelSize zero = ClampPixelSize(0, 0);
    FASTPDF_CHECK_EQ(zero.width, 1);
    FASTPDF_CHECK_EQ(zero.height, 1);
    const PixelSize neg = ClampPixelSize(-5, -5);
    FASTPDF_CHECK_EQ(neg.width, 1);
    FASTPDF_CHECK_EQ(neg.height, 1);
}

FASTPDF_TEST(preview_size_halves_with_min_one) {
    const PixelSize half = PreviewSizeFor(100, 50);
    FASTPDF_CHECK_EQ(half.width, 50);
    FASTPDF_CHECK_EQ(half.height, 25);

    const PixelSize tiny = PreviewSizeFor(1, 1);
    FASTPDF_CHECK_EQ(tiny.width, 1);
    FASTPDF_CHECK_EQ(tiny.height, 1);

    const PixelSize odd = PreviewSizeFor(3, 3);
    FASTPDF_CHECK_EQ(odd.width, 1);
    FASTPDF_CHECK_EQ(odd.height, 1);

    const PixelSize invalid = PreviewSizeFor(0, 0);
    FASTPDF_CHECK_EQ(invalid.width, 1);
    FASTPDF_CHECK_EQ(invalid.height, 1);
}

// ---------------------------------------------------------------------------
// LRU CPU cache
// ---------------------------------------------------------------------------

FASTPDF_TEST(lru_cache_hit_and_miss) {
    CpuBitmapCache cache(1024);
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.contains(Key(0, 10, 10)));
    FASTPDF_CHECK(cache.get(Key(0, 10, 10)) != nullptr);
    FASTPDF_CHECK(cache.get(Key(1, 10, 10)) == nullptr);
}

FASTPDF_TEST(lru_cache_byte_budget_is_enforced) {
    // Each 10x10 bitmap is 400 bytes; budget holds exactly two.
    CpuBitmapCache cache(800);
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.put(Key(1, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.put(Key(2, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.currentBytes() <= cache.maxBytes());
    FASTPDF_CHECK_EQ(cache.size(), 2u);
    // The least recently used entry (page 0) was evicted.
    FASTPDF_CHECK(!cache.contains(Key(0, 10, 10)));
    FASTPDF_CHECK(cache.contains(Key(1, 10, 10)));
    FASTPDF_CHECK(cache.contains(Key(2, 10, 10)));
}

FASTPDF_TEST(lru_cache_touch_moves_to_most_recently_used) {
    CpuBitmapCache cache(800);
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.put(Key(1, 10, 10), MakeBitmap(10, 10)));
    // Touch page 0, then insert page 2; page 1 (least recently used) evicted.
    FASTPDF_CHECK(cache.get(Key(0, 10, 10)) != nullptr);
    FASTPDF_CHECK(cache.put(Key(2, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.contains(Key(0, 10, 10)));
    FASTPDF_CHECK(!cache.contains(Key(1, 10, 10)));
    FASTPDF_CHECK(cache.contains(Key(2, 10, 10)));
}

FASTPDF_TEST(lru_cache_drops_oversized_entry) {
    CpuBitmapCache cache(100);
    const bool accepted = cache.put(Key(0, 100, 100), MakeBitmap(100, 100));
    FASTPDF_CHECK(!accepted);
    FASTPDF_CHECK_EQ(cache.size(), 0u);
    FASTPDF_CHECK_EQ(cache.currentBytes(), 0u);
}

FASTPDF_TEST(lru_cache_replace_recounts_bytes) {
    CpuBitmapCache cache(800);
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));  // 400 bytes
    // Re-inserting the same key replaces the entry without double-counting.
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK_EQ(cache.size(), 1u);
    FASTPDF_CHECK_EQ(cache.currentBytes(), 400u);
    const Bitmap* bmp = cache.get(Key(0, 10, 10));
    FASTPDF_CHECK(bmp != nullptr);
    FASTPDF_CHECK_EQ(bmp->width, 10);
}

FASTPDF_TEST(lru_cache_clear_resets_state) {
    CpuBitmapCache cache(800);
    cache.put(Key(0, 10, 10), MakeBitmap(10, 10));
    cache.put(Key(1, 10, 10), MakeBitmap(10, 10));
    cache.clear();
    FASTPDF_CHECK_EQ(cache.size(), 0u);
    FASTPDF_CHECK_EQ(cache.currentBytes(), 0u);
    FASTPDF_CHECK(!cache.contains(Key(0, 10, 10)));
}

FASTPDF_TEST(lru_cache_hit_rate_and_stats) {
    CpuBitmapCache cache(800);
    FASTPDF_CHECK_EQ(cache.hits(), 0u);
    FASTPDF_CHECK_EQ(cache.misses(), 0u);
    FASTPDF_CHECK(Near(cache.hitRate(), 0.0));

    // Miss on empty
    FASTPDF_CHECK(cache.get(Key(0, 10, 10)) == nullptr);
    FASTPDF_CHECK_EQ(cache.hits(), 0u);
    FASTPDF_CHECK_EQ(cache.misses(), 1u);
    FASTPDF_CHECK(Near(cache.hitRate(), 0.0));

    // Insert and hit
    FASTPDF_CHECK(cache.put(Key(0, 10, 10), MakeBitmap(10, 10)));
    FASTPDF_CHECK(cache.get(Key(0, 10, 10)) != nullptr);
    FASTPDF_CHECK_EQ(cache.hits(), 1u);
    FASTPDF_CHECK_EQ(cache.misses(), 1u);
    FASTPDF_CHECK(Near(cache.hitRate(), 0.5));

    // Another hit -> 2 hits, 1 miss = 66.6%
    FASTPDF_CHECK(cache.get(Key(0, 10, 10)) != nullptr);
    FASTPDF_CHECK_EQ(cache.hits(), 2u);
    FASTPDF_CHECK_EQ(cache.misses(), 1u);
    FASTPDF_CHECK(Near(cache.hitRate(), 2.0 / 3.0));

    // Reset stats
    cache.resetStats();
    FASTPDF_CHECK_EQ(cache.hits(), 0u);
    FASTPDF_CHECK_EQ(cache.misses(), 0u);
}

// ---------------------------------------------------------------------------
// Priority scheduler
// ---------------------------------------------------------------------------

FASTPDF_TEST(scheduler_pops_highest_priority_first) {
    PriorityScheduler scheduler(8);
    scheduler.push({Key(0, 10, 10), RenderPriority::Background, 1, 1});
    scheduler.push({Key(1, 10, 10), RenderPriority::Visible, 1, 2});
    scheduler.push({Key(2, 10, 10), RenderPriority::Adjacent, 1, 3});

    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 1);  // Visible
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 2);  // Adjacent
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 0);  // Background
    FASTPDF_CHECK(!scheduler.pop().has_value());
}

FASTPDF_TEST(scheduler_is_fifo_within_priority) {
    PriorityScheduler scheduler(8);
    scheduler.push({Key(0, 10, 10), RenderPriority::Visible, 1, 1});
    scheduler.push({Key(1, 10,10), RenderPriority::Visible, 1, 2});
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 0);
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 1);
}

FASTPDF_TEST(scheduler_coalesces_duplicate_keys) {
    PriorityScheduler scheduler(8);
    scheduler.push({Key(5, 10, 10), RenderPriority::Adjacent, 1, 10});
    scheduler.push({Key(5, 10, 10), RenderPriority::Visible, 2, 20});
    FASTPDF_CHECK_EQ(scheduler.size(), 1u);
    const auto job = scheduler.pop();
    FASTPDF_CHECK(job.has_value());
    FASTPDF_CHECK_EQ(job->key.pageIndex, 5);
    FASTPDF_CHECK_EQ(job->priority, RenderPriority::Visible);  // highest wins
    FASTPDF_CHECK_EQ(job->jobId, 20u);                        // newest wins
    FASTPDF_CHECK_EQ(job->viewEpoch, 2u);
}

FASTPDF_TEST(scheduler_is_bounded_and_drops_low_priority) {
    PriorityScheduler scheduler(2);
    scheduler.push({Key(0, 10, 10), RenderPriority::Visible, 1, 1});
    scheduler.push({Key(1, 10, 10), RenderPriority::Visible, 1, 2});
    // A background job cannot displace any visible job: it is dropped.
    scheduler.push({Key(2, 10, 10), RenderPriority::Background, 1, 3});
    FASTPDF_CHECK_EQ(scheduler.size(), 2u);
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 0);
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 1);
}

FASTPDF_TEST(scheduler_evicts_lowest_priority_when_full) {
    PriorityScheduler scheduler(2);
    scheduler.push({Key(0, 10, 10), RenderPriority::Background, 1, 1});
    scheduler.push({Key(1, 10, 10), RenderPriority::Background, 1, 2});
    // A visible job displaces one of the background jobs.
    scheduler.push({Key(2, 10, 10), RenderPriority::Visible, 1, 3});
    FASTPDF_CHECK_EQ(scheduler.size(), 2u);
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 2);  // Visible first
}

FASTPDF_TEST(scheduler_remove_stale_view_epochs) {
    PriorityScheduler scheduler(8);
    scheduler.push({Key(0, 10, 10), RenderPriority::Visible, 1, 1});
    scheduler.push({Key(1, 10, 10), RenderPriority::Visible, 2, 2});
    scheduler.push({Key(2, 10, 10), RenderPriority::Visible, 1, 3});
    scheduler.removeStaleViewEpochs(2);
    FASTPDF_CHECK_EQ(scheduler.size(), 1u);
    FASTPDF_CHECK_EQ(scheduler.pop()->key.pageIndex, 1);
}

FASTPDF_TEST(scheduler_clear_drops_all) {
    PriorityScheduler scheduler(4);
    scheduler.push({Key(0, 10, 10), RenderPriority::Visible, 1, 1});
    scheduler.clear();
    FASTPDF_CHECK_EQ(scheduler.size(), 0u);
    FASTPDF_CHECK(!scheduler.pop().has_value());
}

// ---------------------------------------------------------------------------
// Idle debounce (preview -> final promotion)
// ---------------------------------------------------------------------------

FASTPDF_TEST(idle_debounce_promotes_after_threshold) {
    IdleDebounce debounce(130);
    debounce.NoteActivity(1000);
    FASTPDF_CHECK(!debounce.ShouldPromote(1100));
    FASTPDF_CHECK(!debounce.ShouldPromote(1129));
    FASTPDF_CHECK(debounce.ShouldPromote(1130));
}

FASTPDF_TEST(idle_debounce_resets_on_activity) {
    IdleDebounce debounce(130);
    debounce.NoteActivity(1000);
    FASTPDF_CHECK(!debounce.ShouldPromote(1050));  // only 50 ms idle
    debounce.NoteActivity(1100);                  // activity resets the window
    FASTPDF_CHECK(!debounce.ShouldPromote(1200));  // only 100 ms idle
    FASTPDF_CHECK(debounce.ShouldPromote(1230));   // 130 ms since last activity
}

} // namespace

int main() { return fastpdf::test::RunAll(); }

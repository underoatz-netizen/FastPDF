#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>

#include "fastpdf/renderer/bitmap.h"
#include "fastpdf/renderer/render_key.h"

namespace fastpdf::renderer {

// Byte-bounded least-recently-used cache of rendered CPU bitmaps keyed by
// RenderKey. Total stored bytes never exceed the configured budget: inserting
// an entry that would overflow evicts least-recently-used entries until it
// fits, and an entry that alone exceeds the budget is dropped.
//
// Not thread-safe by design: the UI thread owns the cache. The render worker
// only produces bitmaps that the UI inserts, which keeps shared-state
// synchronization off the hot draw path.
class CpuBitmapCache {
public:
    explicit CpuBitmapCache(std::size_t maxBytes) noexcept;

    CpuBitmapCache(const CpuBitmapCache&) = delete;
    CpuBitmapCache& operator=(const CpuBitmapCache&) = delete;

    // Returns the cached bitmap for |key| if present and marks it most
    // recently used. The returned pointer is invalidated by the next put(),
    // erase(), or clear() on this cache. Returns nullptr when absent.
    const Bitmap* get(const RenderKey& key) noexcept;

    // Inserts (or replaces) |key| -> |bitmap|, evicting LRU entries as needed
    // to respect the byte budget. Returns false when the entry is dropped (its
    // own size exceeds the budget, or it is empty/unaccountable).
    bool put(const RenderKey& key, Bitmap bitmap);

    void erase(const RenderKey& key) noexcept;
    void clear() noexcept;

    bool contains(const RenderKey& key) const noexcept;
    std::size_t size() const noexcept { return map_.size(); }
    std::size_t currentBytes() const noexcept { return currentBytes_; }
    std::size_t maxBytes() const noexcept { return maxBytes_; }

    // Diagnostic cache statistics
    std::uint64_t hits() const noexcept { return hits_; }
    std::uint64_t misses() const noexcept { return misses_; }
    double hitRate() const noexcept {
        const std::uint64_t total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / static_cast<double>(total) : 0.0;
    }
    void resetStats() noexcept { hits_ = 0; misses_ = 0; }

private:
    struct Node {
        RenderKey key;
        Bitmap bitmap;
        std::size_t bytes = 0;
    };

    void EvictToFit(std::size_t neededBytes) noexcept;
    void Touch(std::list<Node>::iterator it) noexcept;

    std::size_t maxBytes_ = 0;
    std::size_t currentBytes_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::list<Node> lru_;  // front = most recently used
    std::unordered_map<RenderKey, std::list<Node>::iterator, RenderKeyHash> map_;
};

}  // namespace fastpdf::renderer

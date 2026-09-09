#include "fastpdf/renderer/lru_cache.h"

namespace fastpdf::renderer {

CpuBitmapCache::CpuBitmapCache(std::size_t maxBytes) noexcept
    : maxBytes_(maxBytes) {}

const Bitmap* CpuBitmapCache::get(const RenderKey& key) noexcept {
    const auto it = map_.find(key);
    if (it == map_.end()) {
        ++misses_;
        return nullptr;
    }
    ++hits_;
    Touch(it->second);
    return &it->second->bitmap;
}

bool CpuBitmapCache::put(const RenderKey& key, Bitmap bitmap) {
    const std::size_t bytes = bitmap.sizeBytes();
    if (bytes == 0) {
        return false;  // Empty/unaccountable bitmap is not cacheable.
    }

    // Replacing an existing key: remove it first so its bytes are not double
    // counted while evicting below.
    const auto existing = map_.find(key);
    if (existing != map_.end()) {
        currentBytes_ -= existing->second->bytes;
        lru_.erase(existing->second);
        map_.erase(existing);
    }

    if (bytes > maxBytes_) {
        return false;  // A single entry exceeds the whole budget; drop it.
    }

    EvictToFit(bytes);

    lru_.push_front(Node{key, std::move(bitmap), bytes});
    map_.emplace(key, lru_.begin());
    currentBytes_ += bytes;
    return true;
}

void CpuBitmapCache::erase(const RenderKey& key) noexcept {
    const auto it = map_.find(key);
    if (it == map_.end()) {
        return;
    }
    currentBytes_ -= it->second->bytes;
    lru_.erase(it->second);
    map_.erase(it);
}

void CpuBitmapCache::clear() noexcept {
    lru_.clear();
    map_.clear();
    currentBytes_ = 0;
}

bool CpuBitmapCache::contains(const RenderKey& key) const noexcept {
    return map_.find(key) != map_.end();
}

void CpuBitmapCache::EvictToFit(std::size_t neededBytes) noexcept {
    while (!lru_.empty() && currentBytes_ + neededBytes > maxBytes_) {
        const auto back = std::prev(lru_.end());  // least recently used
        currentBytes_ -= back->bytes;
        map_.erase(back->key);
        lru_.erase(back);
    }
}

void CpuBitmapCache::Touch(std::list<Node>::iterator it) noexcept {
    lru_.splice(lru_.begin(), lru_, it);
}

}  // namespace fastpdf::renderer

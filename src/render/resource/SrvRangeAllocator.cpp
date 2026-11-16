#include "SrvRangeAllocator.h"

namespace rs::render {

void SrvRangeAllocator::Init(uint32_t firstIndex, uint32_t capacity) {
    firstIndex_ = firstIndex;
    capacity_ = capacity;
    usedSlots_ = 0;
    bumpNext_ = 0;
    for (std::vector<uint32_t>& list : freeLists_) list.clear();
}

size_t SrvRangeAllocator::ClassIndexFor(uint32_t requestedSlots) const {
    for (size_t i = 0; i < kSizeClasses.size(); ++i) {
        if (kSizeClasses[i] >= requestedSlots) return i;
    }
    return kSizeClasses.size();
}

SrvRangeAllocator::Range SrvRangeAllocator::Allocate(uint32_t requestedSlots) {
    Range result;
    const size_t classIndex = ClassIndexFor(requestedSlots);
    if (classIndex >= kSizeClasses.size()) return result; // 末尾クラス超過

    const uint32_t blockSlots = kSizeClasses[classIndex];

    std::vector<uint32_t>& freeList = freeLists_[classIndex];
    if (!freeList.empty()) {
        const uint32_t base = freeList.back();
        freeList.pop_back();
        result.baseIndex = base;
        result.capacity = blockSlots;
        result.ok = true;
        usedSlots_ += blockSlots;
        return result;
    }

    if (bumpNext_ + blockSlots > capacity_) return result; // 容量枯渇

    result.baseIndex = firstIndex_ + bumpNext_;
    result.capacity = blockSlots;
    result.ok = true;
    bumpNext_ += blockSlots;
    usedSlots_ += blockSlots;
    return result;
}

void SrvRangeAllocator::Free(const Range& range) {
    if (!range.ok) return;
    const size_t classIndex = ClassIndexFor(range.capacity);
    if (classIndex >= kSizeClasses.size() || kSizeClasses[classIndex] != range.capacity) {
        return; // Allocate()由来でないRangeは無視(呼び出し側の不整合)
    }
    freeLists_[classIndex].push_back(range.baseIndex);
    usedSlots_ -= range.capacity;
}

} // namespace rs::render

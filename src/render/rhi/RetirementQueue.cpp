#include "RetirementQueue.h"

#include <algorithm>
#include <utility>

namespace rs::rhi {

void RetirementQueue::Retire(std::shared_ptr<void> resource, std::function<void()> release) {
    if (resource || release) entries_.push_back({retireFenceValue_, std::move(resource), std::move(release)});
}

void RetirementQueue::ReleaseCompleted(uint64_t completedFenceValue) {
    std::erase_if(entries_, [completedFenceValue](Entry& entry) {
        if (entry.fenceValue > completedFenceValue) return false;
        entry.resource.reset();
        if (entry.release) entry.release();
        return true;
    });
}

} // namespace rs::rhi

#include "FlareTextureSlots.h"

#include <algorithm>

namespace rs::app {

std::uint32_t FlareTextureSlots::Assign(const std::string& path) {
    const std::vector<std::string>::const_iterator found = std::find(paths_.begin(), paths_.end(), path);
    if (found != paths_.end()) return static_cast<std::uint32_t>(found - paths_.begin());
    if (paths_.size() >= rs::render::kMaxFlareTextures) {
        overflowed_ = true;
        return kUnassigned;
    }
    paths_.push_back(path);
    return static_cast<std::uint32_t>(paths_.size() - 1);
}

std::vector<FlareTextureSlots::Pending> FlareTextureSlots::TakePending() {
    std::vector<Pending> pending;
    for (std::size_t slot = uploadedCount_; slot < paths_.size(); ++slot) {
        pending.push_back({static_cast<std::uint32_t>(slot), paths_[slot]});
    }
    uploadedCount_ = paths_.size();
    return pending;
}

bool FlareTextureSlots::TakeOverflowNotice() {
    if (!overflowed_ || overflowNotified_) return false;
    overflowNotified_ = true;
    return true;
}

}  // namespace rs::app

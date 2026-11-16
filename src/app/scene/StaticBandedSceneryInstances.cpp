//
// StaticBandedSceneryInstances.cpp
//
#include "StaticBandedSceneryInstances.h"

#include <algorithm>

namespace rs::app {

void StaticBandedSceneryInstances::Reset() {
    assigner_.Clear();
    worlds_.clear();
}

size_t StaticBandedSceneryInstances::AddGroup(const std::string& baseKey,
                                              const StaticMeshVariantSet& variants) {
    return assigner_.AddGroup(baseKey, variants.bandStarts, variants.variantOfBand,
                              variants.variants.size(), variants.sharedStateMask);
}

void StaticBandedSceneryInstances::AddMember(size_t groupIndex, const float world[16]) {
    const float position[3] = {world[12], world[13], world[14]};
    assigner_.AddMember(groupIndex, position);
    std::array<float, 16> copy;
    std::copy(world, world + 16, copy.begin());
    worlds_.push_back(copy);
}

void StaticBandedSceneryInstances::SetView(const float eye[3], float viewerFovYRadians) {
    std::copy(eye, eye + 3, eye_);
    fovYRadians_ = viewerFovYRadians;
    hasView_ = true;
}

size_t StaticBandedSceneryInstances::ApplyPending(
    double nowSeconds, double intervalSeconds, const rs::world::SceneSharedSwitchValues& shared,
    const ClearKeysFn& clearKeys, const AddInstanceFn& addInstance) {
    if (!hasView_ || assigner_.MemberCount() == 0) return 0;
    if (!assigner_.DueForUpdate(nowSeconds, intervalSeconds, shared)) return 0;
    const std::unordered_set<std::string> changedKeys =
        assigner_.Update(eye_, fovYRadians_, nowSeconds, shared);
    if (changedKeys.empty()) return 0;
    clearKeys(changedKeys);
    for (const std::string& key : changedKeys) {
        for (const size_t member : assigner_.MembersOfKey(key)) {
            addInstance(key, worlds_[member].data());
        }
    }
    return changedKeys.size();
}

} // namespace rs::app

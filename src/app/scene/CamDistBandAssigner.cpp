//
// CamDistBandAssigner.cpp
//
#include "CamDistBandAssigner.h"

#include <algorithm>

#include "../../plugins/schema/CamDistBands.h"
#include "../plugin_session/SharedStateAxes.h"
#include "StaticInstanceKey.h"

namespace rs::app {

size_t CamDistBandAssigner::AddGroup(std::string baseKey, std::vector<int32_t> bandStarts,
                                     std::vector<uint32_t> variantOfBand, size_t variantCount,
                                     uint32_t sharedStateMask) {
    Group group;
    group.bandMap.bandStarts = std::move(bandStarts);
    group.bandMap.variantOfBand = std::move(variantOfBand);
    group.bandMap.sharedStateMask = sharedStateMask;
    unionSharedMask_ |= sharedStateMask;
    needsUpdate_ = true;
    const size_t count = std::max<size_t>(variantCount, 1);
    group.variantKeys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        group.variantKeys.push_back(MakeStaticVariantKey(baseKey, i, variantCount));
    }
    groups_.push_back(std::move(group));
    return groups_.size() - 1;
}

size_t CamDistBandAssigner::AddMember(size_t groupIndex, const float position[3]) {
    Member member;
    member.group = groupIndex;
    member.position[0] = position[0];
    member.position[1] = position[1];
    member.position[2] = position[2];
    members_.push_back(member);
    needsUpdate_ = true;
    return members_.size() - 1;
}

void CamDistBandAssigner::Clear() {
    groups_.clear();
    members_.clear();
    membersByKey_.clear();
    unionSharedMask_ = 0;
    lastSharedSlot_ = 0;
    needsUpdate_ = true;
    lastUpdateSeconds_ = 0.0;
}

size_t CamDistBandAssigner::UnionSharedSlot(
    const rs::world::SceneSharedSwitchValues& shared) const {
    return SharedStateSlotIndex(unionSharedMask_, shared);
}

bool CamDistBandAssigner::DueForUpdate(double nowSeconds, double intervalSeconds,
                                       const rs::world::SceneSharedSwitchValues& shared) const {
    if (needsUpdate_ || intervalSeconds <= 0.0) return true;
    if (UnionSharedSlot(shared) != lastSharedSlot_) return true;
    return nowSeconds - lastUpdateSeconds_ >= intervalSeconds;
}

std::unordered_set<std::string> CamDistBandAssigner::Update(
    const float eye[3], float viewerFovYRadians, double nowSeconds,
    const rs::world::SceneSharedSwitchValues& shared) {
    // グループごとの現在の状態スロット(メンバごとには引き直さない)。
    std::vector<size_t> stateSlotOfGroup;
    stateSlotOfGroup.reserve(groups_.size());
    for (const Group& group : groups_)
        stateSlotOfGroup.push_back(SharedStateSlotIndex(group.bandMap.sharedStateMask, shared));

    std::unordered_set<std::string> changedKeys;
    for (Member& member : members_) {
        const Group& group = groups_[member.group];
        // _CAMDISTを参照しないグループは位置からバンドを求めない。
        int32_t bandStart = 0;
        if (group.bandMap.ReferencesCamDist()) {
            const int32_t rawCamDist =
                rs::legacy_pi::ComputeScaledCamDistance(eye, member.position, viewerFovYRadians);
            bandStart = rs::legacy_pi::QuantizeCamDistToBand(rawCamDist, group.bandMap.bandStarts,
                                                             true);
        }
        const size_t variant = std::min(
            group.bandMap.VariantIndex(stateSlotOfGroup[member.group], bandStart),
            group.variantKeys.size() - 1);
        const int64_t previous = member.assignedVariant;
        if (previous == static_cast<int64_t>(variant)) continue;
        if (previous >= 0) changedKeys.insert(group.variantKeys[static_cast<size_t>(previous)]);
        changedKeys.insert(group.variantKeys[variant]);
        member.assignedVariant = static_cast<int64_t>(variant);
    }

    if (!changedKeys.empty()) {
        // 所属の一覧は追加順を保って作り直す。
        membersByKey_.clear();
        for (size_t i = 0; i < members_.size(); ++i) {
            const Group& group = groups_[members_[i].group];
            membersByKey_[group.variantKeys[static_cast<size_t>(members_[i].assignedVariant)]]
                .push_back(i);
        }
    }
    needsUpdate_ = false;
    lastSharedSlot_ = UnionSharedSlot(shared);
    lastUpdateSeconds_ = nowSeconds;
    return changedKeys;
}

const std::vector<size_t>& CamDistBandAssigner::MembersOfKey(const std::string& variantKey) const {
    static const std::vector<size_t> kEmpty;
    std::unordered_map<std::string, std::vector<size_t>>::const_iterator it =
        membersByKey_.find(variantKey);
    return it == membersByKey_.end() ? kEmpty : it->second;
}

const float* CamDistBandAssigner::MemberPosition(size_t memberIndex) const {
    return members_[memberIndex].position;
}

} // namespace rs::app

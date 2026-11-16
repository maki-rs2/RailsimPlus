//
// StaticHeadlightRoster.cpp
//
#include "StaticHeadlightRoster.h"

#include "../../plugins/schema/CamDistBands.h"
#include "../plugin_session/SharedStateAxes.h"

namespace rs::app {

void StaticHeadlightRoster::Set(const rs::world::Guid& entityId, bool signalModel,
                                const StaticMeshVariantSet& variants) {
    Entry entry;
    entry.signalModel = signalModel;
    entry.bandStarts = variants.bandStarts;
    entry.sharedStateMask = variants.sharedStateMask;
    entry.headlightsOfVariant = variants.headlightsOfVariant;
    entries_[entityId] = std::move(entry);
}

const std::vector<StaticHeadlight>* StaticHeadlightRoster::Entry::Select(
    const rs::world::SceneSharedSwitchValues& shared, bool hasView, const float eye[3], float fovYRadians,
    const float position[3]) const {
    const size_t stateSlot = SharedStateSlotIndex(sharedStateMask, shared);
    // _CAMDISTを参照しない個体は位置から距離の区分を求めない。求め方は CamDistBandAssigner::Update と同じ。
    size_t band = 0;
    if (!bandStarts.empty() && hasView) {
        const int32_t rawCamDist = rs::legacy_pi::ComputeScaledCamDistance(eye, position, fovYRadians);
        const int32_t bandStart = rs::legacy_pi::QuantizeCamDistToBand(rawCamDist, bandStarts, true);
        band = BandIndexOfStart(bandStarts, bandStart);
    }
    const size_t bandCount = bandStarts.empty() ? 1 : bandStarts.size();
    const size_t index = stateSlot * bandCount + band;
    if (index >= headlightsOfVariant.size()) return nullptr;
    return headlightsOfVariant[index].get();
}

} // namespace rs::app

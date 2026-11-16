//
// SceneSlotState.cpp - シーン常駐スロットのGPU非依存状態
//
#include "SceneSlotState.h"

namespace rs::app {

void SceneSlotState::GrowToInclude(size_t slot) {
    while (slot >= scenePartWorldDirty_.size()) {
        scenePartWorldDirty_.push_back({true, true});
        scenePartWorldKeys_.push_back(0);
        sceneSlotVisible_.push_back(true);
        sceneSlotFadeAlpha_.push_back(1.0f);
        sceneSlotBounds_.emplace_back();
        sceneSlotCulled_.push_back(0);
    }
}

void SceneSlotState::ClearPartWorldDirty() {
    scenePartWorldDirty_.clear();
}

void SceneSlotState::ClearPartWorldKeys() {
    scenePartWorldKeys_.clear();
}

void SceneSlotState::ClearBounds() {
    sceneSlotBounds_.clear();
}

void SceneSlotState::ClearCulled() {
    sceneSlotCulled_.clear();
}

void SceneSlotState::ResetLastCullStats() {
    lastCullStats_ = {};
}

} // namespace rs::app

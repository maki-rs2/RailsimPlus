//
// SceneSlotState.h - シーン常駐スロットのGPU非依存状態
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "SceneSlotCulling.h"

namespace rs::app {

class SceneSlotState {
public:
    void GrowToInclude(size_t slot);
    void ClearPartWorldDirty();
    void ClearPartWorldKeys();
    void ClearBounds();
    void ClearCulled();
    void ResetLastCullStats();

    std::vector<std::array<bool, 2>>& PartWorldDirty() { return scenePartWorldDirty_; }
    const std::vector<std::array<bool, 2>>& PartWorldDirty() const { return scenePartWorldDirty_; }
    // スロットごとの連続キー。パーツ行列の並びが前回書いたものと対応しているかを表す
    // (スロットの中身が入れ替わるたびに変わる値を、書き込む側が渡す)。
    std::vector<uint64_t>& PartWorldKeys() { return scenePartWorldKeys_; }
    const std::vector<uint64_t>& PartWorldKeys() const { return scenePartWorldKeys_; }
    std::vector<bool>& Visible() { return sceneSlotVisible_; }
    const std::vector<bool>& Visible() const { return sceneSlotVisible_; }
    std::vector<float>& FadeAlpha() { return sceneSlotFadeAlpha_; }
    const std::vector<float>& FadeAlpha() const { return sceneSlotFadeAlpha_; }
    std::vector<SceneSlotBounds>& Bounds() { return sceneSlotBounds_; }
    const std::vector<SceneSlotBounds>& Bounds() const { return sceneSlotBounds_; }
    std::vector<uint8_t>& Culled() { return sceneSlotCulled_; }
    const std::vector<uint8_t>& Culled() const { return sceneSlotCulled_; }
    SceneSlotCullStats& LastCullStats() { return lastCullStats_; }
    const SceneSlotCullStats& LastCullStats() const { return lastCullStats_; }

private:
    std::vector<std::array<bool, 2>> scenePartWorldDirty_;
    std::vector<uint64_t> scenePartWorldKeys_;
    std::vector<bool> sceneSlotVisible_;
    std::vector<float> sceneSlotFadeAlpha_;
    std::vector<SceneSlotBounds> sceneSlotBounds_;
    std::vector<uint8_t> sceneSlotCulled_;
    SceneSlotCullStats lastCullStats_;
};

} // namespace rs::app

//
// PlacementHeightAction.cpp - 配置・敷設の高さオフセット調整
//
#include "PlacementHeightAction.h"

#include <algorithm>
#include <cmath>

#include "../../editor/tool/Editor.h"
#include "../../editor/tool/NodeDragPlan.h"

namespace rs::app {

float* PlacementHeightAction::TargetOffset() const {
    // ノードのドラッグ中が最優先(ドラッグは他のモードより短い操作で、押している間だけ有効)。
    // 次に施設配置、敷設ツールの順(両方立っていたら施設配置を優先する)。
    if (editor_.nodeDragUi.active) return &editor_.nodeDragUi.heightOffset;
    if (editor_.sceneryPlacementMode) return &editor_.sceneryPlacementHeightOffset;
    if (editor_.mode == rs::editor::ToolMode::PlaceArc) return &editor_.layingSettings.placeHeightOffset;
    return nullptr;
}

bool PlacementHeightAction::HasSelectedNodeTarget() const {
    if (TargetOffset() != nullptr) return false;
    if (editor_.mode != rs::editor::ToolMode::Select) return false;
    if (rs::world::GuidIsNil(editor_.selectedNode)) return false;
    return editor_.graph.nodes.find(editor_.selectedNode) != editor_.graph.nodes.end();
}

bool PlacementHeightAction::HasTarget() const {
    return TargetOffset() != nullptr || HasSelectedNodeTarget();
}

bool PlacementHeightAction::AdjustSelectedNode(int steps) {
    const float current = editor_.graph.nodes.at(editor_.selectedNode).pos.y;
    // 倍数へ寄せてから動かし、許容範囲へ収める。形を作れない標高はEditor側が拒否する。
    const long long currentSteps = std::llround(static_cast<double>(current) / kStepMeters);
    const float target = static_cast<float>(currentSteps + steps) * kStepMeters;
    editor_.SetSelectedNodeElevation(
        std::clamp(target, rs::editor::kMinNodeElevationM, rs::editor::kMaxNodeElevationM));
    return true;
}

bool PlacementHeightAction::AdjustSteps(int steps) {
    float* offset = TargetOffset();
    if (offset == nullptr) return HasSelectedNodeTarget() && AdjustSelectedNode(steps);
    // 既存の値が倍数でなくても、最も近い倍数へ寄せてから動かす(誤差を積まない)。
    const long long currentSteps = std::llround(static_cast<double>(*offset) / kStepMeters);
    *offset = static_cast<float>(currentSteps + steps) * kStepMeters;
    // ノードのドラッグ中は、確定する高さ(開始高さ+オフセット)が許容範囲に収まるようにする。
    if (offset == &editor_.nodeDragUi.heightOffset) {
        const float planeY = editor_.nodeDragUi.planeY;
        *offset = std::clamp(*offset, rs::editor::kMinNodeElevationM - planeY,
                             rs::editor::kMaxNodeElevationM - planeY);
    }
    return true;
}

bool PlacementHeightAction::AdjustByWheel(float notches) {
    if (!HasTarget()) {
        wheelRemainder_ = 0.0f;
        return false;
    }
    wheelRemainder_ += notches;
    const float whole = std::trunc(wheelRemainder_);
    wheelRemainder_ -= whole;
    return AdjustSteps(static_cast<int>(whole));
}

void PlacementHeightAction::UpdateKeys(bool upHeld, bool downHeld, double dtSeconds) {
    const int direction = (upHeld == downHeld) ? 0 : (upHeld ? 1 : -1);
    if (direction == 0 || !HasTarget()) {
        keyStepper_.Reset();
        return;
    }
    AdjustSteps(keyStepper_.Update(direction, dtSeconds));
}

} // namespace rs::app

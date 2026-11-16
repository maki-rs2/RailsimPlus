//
// PlacementHeightAction.h - 配置・敷設の高さオフセットを「↑↓キー / Ctrl+ホイール」で動かす
//
// 施設配置プレビュー(Editor::sceneryPlacementHeightOffset)・敷設ツール
// (TrackLayingSettings::placeHeightOffset)・ノードのドラッグ中(NodeDragUiState::heightOffset)が
// 同じ操作・同じ刻みで高さを調整できるように、
// 動かす先の選択・刻み・キー長押しの連続送りをここ1か所に持つ。
// 上のどれも無く、選択ノードがあるときは、そのノードの標高(絶対値)を動かす
// (範囲はkMinNodeElevationM〜kMaxNodeElevationM)。
//
#pragma once

#include "KeyRepeatStepper.h"

namespace rs::editor { class Editor; }

namespace rs::app {

class PlacementHeightAction {
public:
    // 1歩の高さ[m]。施設配置のCtrl+ホイール1ノッチと同じ刻み。
    static constexpr float kStepMeters = 0.2f;

    explicit PlacementHeightAction(rs::editor::Editor& editor) : editor_(editor) {}

    // 動かす先があるか(ノードのドラッグ中、施設配置モード、敷設ツール、または選択ノード)。
    bool HasTarget() const;

    // 符号付きの歩数だけ動かす。結果は常にkStepMetersの倍数になる。動かす先が無ければ
    // 何もせずfalseを返す。
    bool AdjustSteps(int steps);

    // ホイールのノッチ数(小数あり)で動かす。1歩に満たない端数は次回へ持ち越す。
    bool AdjustByWheel(float notches);

    // キー1フレーム分の更新。押した瞬間に1歩、PlacementHeightTuningの
    // repeatDelaySeconds後からrepeatStepsPerSecondで連続送りする。歩数は経過時間の累計から整数で数えるので、
    // フレームの区切り方によらず同じ時間なら同じ歩数になる。上下とも押している間・
    // どちらも押していない間は送らない。動かす先が無いフレームは押下状態を捨てる。
    void UpdateKeys(bool upHeld, bool downHeld, double dtSeconds);

private:
    float* TargetOffset() const;
    // 選択ノードの標高を動かす対象か(TargetOffsetが無いときだけ)。
    bool HasSelectedNodeTarget() const;
    bool AdjustSelectedNode(int steps);

    rs::editor::Editor& editor_;
    float wheelRemainder_ = 0.0f;
    KeyRepeatStepper keyStepper_;
};

} // namespace rs::app

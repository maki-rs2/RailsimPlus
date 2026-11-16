// HeadlightWorldTracker.h - Headlight(灯具)のワールド位置と向きの追跡。
#pragma once

#include <vector>

#include "HeadlightAttachment.h"

namespace rs::app {

struct HeadlightRuntimeState {
    rs::core::Vec3 worldPos{0, 0, 0};
    rs::core::Vec3 worldDir{0, 0, 1};
    // 一度でも位置を求めたか。偽の間は原点のままなので、灯具として出さない。
    bool hasWorld = false;
};

// 灯具1件のワールド位置と向き。partWorld は def.parts 添字の部品行列、entityWorld はその外側にかける配置の行列。
// 動く部品(UpdateHeadlightWorldPoses)と静的バッチの配置(信号のモデル)が、同じ式を使う。
HeadlightRuntimeState ComputeHeadlightWorldPose(const HeadlightAttachment& attachment,
                                                const rs::core::Mat4& partWorld,
                                                const rs::core::Mat4& entityWorld);

// RailSim II の灯具と同じ式。位置 = 部品の位置 + SourceCoord.x×右 + .y×上 + .z×前、向き = Direction を同じ軸で
// 変換して正規化(右・上・前は部品のワールド行列の軸を正規化したもの。行ベクトルの規約で行0〜2)。
// dynamicWorlds は def.parts 添字の部品行列、entityWorld はその外側にかける配置の行列。
void UpdateHeadlightWorldPoses(const std::vector<HeadlightAttachment>& attachments,
                               const std::vector<rs::core::Mat4>& dynamicWorlds,
                               const rs::core::Mat4& entityWorld, std::vector<HeadlightRuntimeState>& states);

} // namespace rs::app

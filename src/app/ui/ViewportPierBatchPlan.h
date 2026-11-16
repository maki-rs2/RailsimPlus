//
// ViewportPierBatchPlan.h - Pier(橋脚)のインスタンスバッチキー・高さ丸め・
// buildMinAlt判定を1箇所へ集約する(trackPluginItemsループとSceneryループの
// 両方から同じ規則で使う。重複実装を防ぐ)。
//
#pragma once

#include <string>

#include "../../core/Math.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::app {

// Pier1件分のバッチ計画。entryがnullptr、またはheightMがdef.pier.buildMinAlt未満なら
// skip=trueとなり、呼び出し側はこのitem/entityをバッチ対象から外す。
struct PierBatchPlan {
    std::string key;
    float       roundedHeight = 0.0f;
    bool        skip = true;
};

// folderUtf8のワールド行列(world)から設置点高さ(world.m[13])を読み、0.25m単位に
// 丸めたうえでバッチキー(folderUtf8 + "|" + 丸め高さ)を組み立てる。
PierBatchPlan ComputePierBatchPlan(const std::string& folderUtf8, const TrackPluginEntry* entry,
                                    const rs::core::Mat4& world);

// Pierのローカル原点はシャフト下端(地面)なので、配置時は取付点(レール高さ)では
// なく地面(world y=0)へ置く(シャフト自体はComputePierBatchPlanのroundedHeight分
// 既に押し出し済みのメッシュを使う前提)。
void ZeroPierWorldY(rs::core::Mat4& world);

} // namespace rs::app

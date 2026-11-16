//
// StaticMeshVariantBake.h - 配置プラグイン1つぶんのバンド・共有状態別メッシュ焼き込み
//
#pragma once

#include <cstdint>
#include <vector>

#include "StaticMeshVariantSet.h"
#include "../plugin_session/PluginSession.h"

namespace rs::app {

// 静的インスタンス化できるプラグインなら、共有状態の状態スロット(_NIGHT/_SEASON/_SHADOW/_ENVMAP非参照なら1つ)
// × バンド(_CAMDIST非参照なら1つ)ごとにスイッチ確定値を焼き込み、パートワールドとAABBまで
// 済ませたメッシュ集合をoutへ書いてtrueを返す。
// 条件: plugin.ok && hasAssembly && 可動部/アニメ/音源/Dynamicな軸の参照が無い、かつ
// _CAMDISTを参照するなら量子化が安全(CamDistQuantizationSafe)。満たさなければfalse。
// sessionは組ごとに使い回す(状態はApplySwitchOptionsAndBakeが素の状態へ戻す)。
// stationRuntimeConstantは、駅舎の実行時スイッチを常に0とみなす(ComputeStaticEligibility参照)。
bool BakeStaticMeshVariants(PluginSession& session, const std::vector<int32_t>& switchOptions,
                            StaticMeshVariantSet& out, bool stationRuntimeConstant = false);

// session.plugin.assemblyMeshのコピーへ、sessionの現在のスイッチ状態のパートワールドを
// 焼き込み、AABBまで計算したメッシュを返す(GPUインスタンス化・配置プレビュー・
// レール/橋脚の組立メッシュの共通の焼き込み)。可動部があれば位相を目標まで進め切った姿勢
// (session.moverRuntimesをBuild直後から作り直す)で焼き、本配置の姿勢と一致させる。
// 時間駆動の可動部は初期姿勢のまま。
rs::render::Mesh BakeSessionAssemblyMesh(PluginSession& session);

} // namespace rs::app

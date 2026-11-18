//
// LineHangerPlacement.h - Line(架線)のハンガー(碍子)モデル配置点の算出。
// RailSim II -k-buildのWireframe{}(ハンガー取付点
// トラッキング、Vertex "0.45"{...}のようなラベル付き頂点)とInterval{ WireTracking;
// ModelFileName }を突き合わせ、径間内の各ハンガーのワールド位置/向きを求める。
//
#pragma once

#include <vector>

#include "../../core/Math.h"
#include "../schema/RailProfileSchema.h"

namespace rs::legacy_pi {
struct LineDef;
} // namespace rs::legacy_pi

namespace rs::legacy_plugin {

// 1個のハンガーの配置情報。intervalはlineDef.intervals内の要素を指す(モデルファイル/
// スケールの参照元、呼び出し側で.xロード+PlaceMeshInstanceに使う)。
struct LineHangerPlacement {
    const rs::legacy_pi::RailInterval* interval = nullptr;
    rs::core::Vec3 pos;
    rs::core::Vec3 fwd; // 径間方向(ワイヤーのローカルz増加方向)
    rs::core::Vec3 up;
};

// baseA/baseB/upA/upB はBuildLineWireMeshと同じ意味(ワイヤーのローカルz=0/1に対応する
// ワールド位置、その地点の軌道アップベクトル)。intervals(PluginDefinition::intervals、
// Interval{}はLineプラグインでもトップレベルで共通にパースされる)にWireTracking付きの
// 要素が無ければ何もしない(ハンガー未定義プラグイン、または旧形式Wireframeファイル)。
// 径間長に合うWireframeプロファイル(ハンガー取付点トラッキング)を単一選択し、
// ラベル付きVertexごとにwireTrackingが一致するIntervalを探して配置点を求める。
std::vector<LineHangerPlacement> ComputeLineHangerPlacements(
    const rs::legacy_pi::LineDef& lineDef, const std::vector<rs::legacy_pi::RailInterval>& intervals,
    const rs::core::Vec3& baseA, const rs::core::Vec3& upA, const rs::core::Vec3& baseB,
    const rs::core::Vec3& upB);

} // namespace rs::legacy_plugin

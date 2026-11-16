//
// PointHandMarks.h - 3D画面で分岐の分かれる線ごとに出す「左」「右」の印の、位置・名前・開通状態を求める
//
// RmlUi非依存。左右の行き先は左タブの開通先の行と同じ BuildPointSideLinks から取るので、
// 表示と開通先の切り替えが必ず一致する。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
struct TrackNode;
} // namespace rs::world

namespace rs::app::workspace {

struct PointHandMark {
    int linkIndex = 0;          // 分岐の links の添字。切り替えにそのまま渡す
    std::string label;          // 「左」「右」。両側が分岐のときは「1 左」のように、左タブの開通先の番号を付ける
    bool active = false;        // 今、この線が開通しているか
    rs::core::Vec3 position;    // 分かれる線のレール上の、分岐から一定の距離の点
};

// node の分かれる線ごとの印を、面の順に左、右の順で返す。分岐でなければ空。
// distanceM は分岐からの距離で、線が短いときは線の半分までに収める。レールが解決できない線は飛ばす。
std::vector<PointHandMark> BuildPointHandMarks(const rs::world::TrackGraph& graph, const rs::world::TrackNode& node,
                                               double distanceM);

} // namespace rs::app::workspace

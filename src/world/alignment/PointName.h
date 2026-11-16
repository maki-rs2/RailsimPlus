//
// PointName.h - 分岐(TrackNode のうち IsPoint() のもの)の既定名「分岐 N」を振る。
//
#pragma once

#include "Guid.h"

namespace rs::world {
struct TrackGraph;

// node が分岐で、名前が空なら、同じグラフで使われていない最小の番号で「分岐 N」を振る。
// 振ったら true。分岐でない・すでに名前がある・node が無いときは何もせず false。
bool AssignDefaultPointName(TrackGraph& graph, const Guid& node);

// 名前が空の分岐すべてに「分岐 N」を振る(.rs2の取り込みや古い保存データの読込の後に呼ぶ)。
// 順序は guid の並びに依存させず、ノードの位置に依らず毎回同じ結果にするため、
// 振る順は graph.nodes の走査順ではなく guid の昇順にする。振った数を返す。
int AssignDefaultPointNames(TrackGraph& graph);

}  // namespace rs::world

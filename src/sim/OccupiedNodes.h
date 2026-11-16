//
// OccupiedNodes.h - 編成の占有edge列から「列車が使用中のノード」を求める
//
// 駅のレール接続エフェクタは、列車が使っているノードをつなぎ替えない。ここでは
// 編成が踏んでいるedgeの、形の基準ノード(nodeA/nodeB)と接続側ノード(継ぎ目)の
// 両方を使用中とする。
//
#pragma once

#include <unordered_set>
#include <vector>

#include "../world/alignment/RailLinkEdit.h"
#include "TrainOccupiedPath.h"

namespace rs::sim {

using NodeSet = std::unordered_set<rs::world::Guid, rs::world::GuidHash>;

// 各要素の sceneIndex のグラフからedgeを引き、両端のノードを集める。
// グラフやedgeが見つからない要素は無視する。
NodeSet CollectOccupiedNodes(const std::vector<const TrainOccupiedPath*>& paths,
                             const GraphForScene& graphForScene);

// 集合を値で持つ判定関数を返す。
rs::world::NodeInUseFn MakeNodeInUseFn(NodeSet nodes);

} // namespace rs::sim

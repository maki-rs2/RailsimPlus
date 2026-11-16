#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TrackGraph.h"
#include "WarpLinkTable.h"

namespace rs::world {

// edgeと進む向き。alongがtrueならedgeのs軸の向き(nodeA→nodeB)に進む。
struct DirectedEdge {
    Guid edge = NilGuid();
    bool along = true;
};

// nodeのfromEdge(fromEnd側)と反対の面につながるedgeを、同じ向きの進入として列挙する。
// ワープの相手のnodeに付くedgeも続きとして扱う(warpTableを渡し、相手が同じシーンのときだけ)。
// arrivingがtrueなら、nodeへ向かう側の向きを返す。
void CollectNeighbors(const TrackGraph& graph, const WarpLinkTable* warpTable, size_t sceneIndex,
                      const Guid& nodeGuid, const Guid& fromEdge, int fromEnd, bool arriving,
                      std::vector<DirectedEdge>& out);

// edgeごとの向きの印。bit0=edgeのs軸の向きに進む、bit1=逆向きに進む。
using TravelDirectionMasks = std::unordered_map<Guid, unsigned char, GuidHash>;
constexpr unsigned char kTravelAlongBit = 1;
constexpr unsigned char kTravelAgainstBit = 2;

// startの進行方向と同じ向きの進み方を、つながり(node、warpTableを渡したときは同じシーン内の
// ワープの先)に沿って手前(上流)と先(下流)の両方へたどり、edgeごとに印を付ける。
// allowedEdgesを渡すと、その集合の中のedgeだけをたどる(閉塞の中だけをたどる用途)。
// stopEdgeを渡すと、そのedgeに最初に着いた時点で打ち切る。着かなかったedgeは印に載らない。
TravelDirectionMasks MarkTravelDirection(const TrackGraph& graph, const WarpLinkTable* warpTable,
                                         size_t sceneIndex, const DirectedEdge& start,
                                         const std::unordered_set<Guid, GuidHash>* allowedEdges,
                                         const Guid* stopEdge);

// 基準のedgeでrefDirection(+1=s軸の向き、-1=逆)に進むとき、対象edgeで同じ進行方向が
// s軸の+1か-1かを、つながり(node、warpTableを渡したときは同じシーン内のワープの先)を
// たどって求める。手前(上流)と先(下流)の両方をたどり、対象edgeに着いた時点で打ち切る。
// refEdgeとtargetEdgeが同じならrefDirectionの符号をそのまま返す。
// 基準のedgeがグラフに無い、またはつながっていないときはnullopt(接線の比較にはフォールバックしない)。
std::optional<int> FollowTravelDirection(const TrackGraph& graph, const Guid& refEdge,
                                         int refDirection, const Guid& targetEdge,
                                         const WarpLinkTable* warpTable = nullptr,
                                         size_t sceneIndex = 0);

} // namespace rs::world

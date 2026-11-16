#pragma once

#include <cstddef>
#include <vector>

#include "TrackGraph.h"
#include "WarpLinkTable.h"

namespace rs::world {

// 閉塞の範囲の途切れ目。componentのedgeの端(end: 0=nodeA、1=nodeB)で、その先に続くedgeが
// あるのに閉塞のedgeが1本も無い場所。
struct BlockCutPoint {
    Guid edge = NilGuid();
    int end = 0;
};

// つながりで隣り合う閉塞のedgeのまとまり。edgeは閉塞のedgesの並び順。
struct BlockComponent {
    std::vector<Guid> edges;
    std::vector<BlockCutPoint> cutPoints;  // 閉塞が2成分以上に割れているときだけ入る
};

struct BlockConnectivity {
    Guid block = NilGuid();
    std::vector<BlockComponent> components;  // 閉塞のedgesで最初に現れた順

    bool IsConnected() const { return components.size() <= 1; }
};

// 閉塞のedgesを、nodeの共有とワープ(warpTableを渡したときは同じシーン内の相手のnode)の
// つながりで隣り合うもの同士の連結成分に分ける。進行方向は使わず、信号の向きの判定と
// 同じたどり方(MarkTravelDirection)で隣り合いを決める。graphに無いedgeは数えない。
BlockConnectivity AnalyzeBlockConnectivity(const TrackGraph& graph, const Block& block,
                                           const WarpLinkTable* warpTable = nullptr,
                                           size_t sceneIndex = 0);

} // namespace rs::world

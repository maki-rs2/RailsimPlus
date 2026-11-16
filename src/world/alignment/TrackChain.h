//
// TrackChain.h - TrackGraph上の連結edge列(チェーン)を求める静的な走査ユーティリティ。
//
// 「links.size()==2のnodeを介してつながる区間」を辿り、分岐(links.size()>=3)や
// 行き止まり(links.size()==1)でチェーンを終端する。TrackPlugin(レール系プラグイン)
// の離散配置(Tie/Pier/Pole)がedge境界をまたいで等間隔配置するための土台。
//
// 既存のTrackTraversal::Advance()/SelectNextLinkはポイントのpointActiveLink等に
// 依存する走行時ロジックであり、静的配置(スイッチ状態非依存で常に分岐=終端)には
// 不適切なため再利用しない。
//
#pragma once

#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// チェーン内の1edge。direction: +1=nodeA→nodeB方向, -1=nodeB→nodeA方向。
struct EdgeChainStep {
    Guid edge = NilGuid();
    int direction = 1;
};

struct EdgeChain {
    std::vector<EdgeChainStep> steps;
    bool isLoop = false;
};

// graph.edgesのguidをソートした順に未訪問edgeを走査開始点として選ぶため、
// 同一graphに対しては常に同じチェーン分割・同じ開始edgeが得られる(毎回同じ結果になる)。
std::vector<EdgeChain> BuildEdgeChains(const TrackGraph& graph);

} // namespace rs::world

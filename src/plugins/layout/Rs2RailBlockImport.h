// Rs2RailBlockImport.h - .rs2 RailBlock名から閉塞エンティティを構築する。
#pragma once

#include <vector>

#include "Rs2Types.h"

namespace rs::world {
struct TrackGraph;
}

namespace rs::legacy_layout {

struct AddressMap;

// RailWayの閉塞名から rs::world::Block を生成してgraphへ登録する。
// 同名のRailWayは1つのBlockのedges集合へまとめ、graphに存在するedgeだけを入れる。
// Blockはまとめた名前の順に登録する。RailSim II の閉塞名ロックは全Sceneで共有されるが、
// ここではTrackGraph単位で登録するためScene間では共有しない。
// 戻り値は生成したBlockの数。
int BuildRailBlocksFromNames(rs::world::TrackGraph& graph, const std::vector<Rs2RailWay>& railWays,
                             const AddressMap& addrMap);

} // namespace rs::legacy_layout

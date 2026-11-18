//
// Rs2EdgeBuild.h - Rs2RailWay 1本分の TrackEdge 構築(Splitter再サンプリング・カント復元・
// プラグイン参照・PierLink/PoleLink解決)
//
#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Rs2NodeBuild.h"
#include "Rs2Types.h"
#include "../../world/alignment/TrackEdge.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::legacy_layout {

// PierLink/PoleLink解決に要る索引一式(シーンにつき1度だけ構築し BuildEdge へ渡す)。
struct ImportedPluginIndex {
    std::unordered_map<Rs2Address, const Rs2PlacedInstance*> pierByAddress, poleByAddress;
    std::unordered_map<Rs2Address, Rs2Address> pierPrimaryWay, polePrimaryWay;
    // 駅の番線の端点がつなぐRailWayの端(先頭レールの端0・末尾レールの端1)。駅のレール接続エフェクタ
    // (ConnectRail など)が付け替えた結果の継ぎ目は、必ずこの端に現れる。
    std::set<std::pair<Rs2Address, int>> platformEnds;
};

ImportedPluginIndex BuildImportedPluginIndex(const Rs2Scene& scene);

// way から TrackEdge を作る。端点ノードが未解決なら nodeA/nodeB が Nil のまま返す
// (呼び出し側が判別してスキップする)。
rs::world::TrackEdge BuildEdge(const Rs2RailWay& way, const AddressMap& addrMap,
                               rs::world::TrackGraph& graphNodes,
                               const std::string& installRootUtf8,
                               const ImportedPluginIndex& famIndex,
                               std::vector<std::string>& warnings);

} // namespace rs::legacy_layout

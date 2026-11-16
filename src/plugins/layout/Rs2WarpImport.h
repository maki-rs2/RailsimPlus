//
// Rs2WarpImport.h - レイアウト直下 WarpList(Rs2Warp) → rs::world::WarpLinkTable 変換。
//
// Rs2Import.cpp(400行規約に反するため)とは別ファイルに分離する。ImportTrackGraphが
// Scene単位のAddress→Guid解決マップ(AddressMap)しか持たないのに対し、Warpの両端は
// Scene境界をまたぐため、全SceneのAddressMapを横断して解決する必要がある。
//
#pragma once

#include <string>
#include <vector>

#include "Rs2NodeBuild.h"
#include "Rs2Types.h"
#include "../../world/alignment/WarpLinkTable.h"

namespace rs::legacy_layout {

// レイアウト直下の WarpList を rs::world::WarpLinkTable へ変換する。
// sceneAddresses[i] は scenes[i] を ImportTrackGraph したときの AddressMap
// (Scene配列と同じ順・同じ長さであること)。両端のRailConnector Addressを
// 全Sceneから探して (sceneIndex, Guid) を解決する。
// 解決できない端点・自己リンク(両端が同一)はスキップし warnings へ記録する。
rs::world::WarpLinkTable ImportWarpLinks(const std::vector<Rs2Warp>& warps,
                                         const std::vector<AddressMap>& sceneAddresses,
                                         std::vector<std::string>& warnings);

} // namespace rs::legacy_layout

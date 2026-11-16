//
// Rs2PlacementNormalize.h - .rs2取り込み専用配置物を手動オーバーレイへ正規化する
//
#pragma once

#include <string>
#include <vector>

#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"

namespace rs::app {

struct ImportedWireLine {
    std::string poleAddressA;
    int         branchA = 0;
    std::string poleAddressB;
    int         branchB = 0;
    std::string lineFolderUtf8;
};

struct ImportedWorldAnchoredPole {
    std::string                    address;
    rs::world::PlacedIndividual individual;
};

rs::world::ManualPlacementOverlay NormalizeImportedPlacements(
    rs::world::TrackGraph& graph, const std::vector<ImportedWireLine>& lines,
    std::vector<std::string>& warnings,
    const std::vector<ImportedWorldAnchoredPole>& worldAnchoredPoles = {});

} // namespace rs::app

//
// MaterializeSnapshot.h - 配置物実体化の入力とworld層での適用
//
#pragma once

#include <string>
#include <vector>

#include "ManualPlacementOverlay.h"
#include "../alignment/Guid.h"
#include "../alignment/IntervalList.h"
#include "../alignment/TrackEdge.h"

namespace rs::world {

struct TrackGraph;

// 区間構成（分割位置）を変えず、全spanから指定folderを一つ取り除く。
// 実体化はfolder不在で表現するため、その自動生成入力を除外する。
void RemoveFolder(IntervalList<PluginSet>& sections, const std::string& folderUtf8);

struct MaterializeSnapshot {
    Guid edge = NilGuid();
    std::string folderUtf8;
    std::vector<PlacedIndividual> poles;
    std::vector<PlacedIndividual> piers;
    std::vector<WireSpan> lines;
    RunAnchor section;

    bool Empty() const;
};

bool ApplyMaterializeSnapshot(TrackGraph& graph, ManualPlacementOverlay& overlay,
                              const MaterializeSnapshot& snapshot,
                              IntervalList<PluginSet>& savedSections);
void RevertMaterializeSnapshot(TrackGraph& graph, ManualPlacementOverlay& overlay,
                               const MaterializeSnapshot& snapshot,
                               const IntervalList<PluginSet>& savedSections);
void DematerializeEdgeFolder(TrackGraph& graph, ManualPlacementOverlay& overlay,
                             const Guid& edge, const std::string& folderUtf8);

void RescaleMaterializedEdgeItems(ManualPlacementOverlay& overlay, const Guid& edge,
                                  double oldLen, double newLen);

} // namespace rs::world

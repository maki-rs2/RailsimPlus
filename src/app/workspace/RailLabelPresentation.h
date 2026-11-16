#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RailLabelHover.h"
#include "../../world/alignment/BlockConnectivity.h"

namespace rs::app::workspace {

bool CanCommitBlockName(const rs::world::Block* block, std::string_view name);

struct RailLabelItem {
    rs::world::Guid guid = rs::world::NilGuid();
    std::string name;
    bool selected = false;
    bool warning = false;
    std::string colorClass;
    RailLabelAnchor anchor;
    std::size_t verticalIndex = 0;
    bool candidate = false;
    bool disabled = false;
    std::string title;
};

struct RailLabelCandidate {
    rs::world::Guid block = rs::world::NilGuid();
    std::string blockedMessage;
};

std::vector<RailLabelItem> BuildRailLabelCandidateItems(const rs::world::TrackGraph& graph,
    const RailLabelAnchor& anchor, std::span<const RailLabelCandidate> candidates);

// 名前の既定値と順序はgraph.blocksから決める。接続性は描画側と共有する計算済みの結果を受ける。
std::vector<RailLabelItem> BuildRailLabelItems(const rs::world::TrackGraph& graph,
                                              std::span<const RailLabel> labels,
                                              const rs::world::Guid& selectedBlock,
                                              std::span<const rs::world::BlockConnectivity> connectivity);

struct RailLabelSize {
    double width = 0.0;
    double height = 0.0;
};

struct RailLabelStack {
    RailLabelSize size;
    std::vector<double> tops;
};

// 同じアンカーのラベルを上から順に積む。全体をまとめてクランプすれば、端でもラベル同士が重ならない。
RailLabelStack BuildRailLabelStack(std::span<const RailLabelSize> sizes, double gap);

} // namespace rs::app::workspace

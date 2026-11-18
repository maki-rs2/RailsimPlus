#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RailLabelHover.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct Block;
struct TrackGraph;
} // namespace rs::world

namespace rs::app::workspace {

bool CanCommitBlockName(const rs::world::Block* block, std::string_view name);

// 閉塞の表示名。名前が空なら、graph.blocks での位置(0始まりの index)から「閉塞 N」と出す。
std::string BlockDisplayName(const rs::world::Block& block, std::size_t index);

struct RailLabelItem {
    rs::world::Guid guid = rs::world::NilGuid();
    std::string name;
    bool selected = false;
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

// 名前の既定値と順序はgraph.blocksから決める。
std::vector<RailLabelItem> BuildRailLabelItems(const rs::world::TrackGraph& graph,
                                              std::span<const RailLabel> labels,
                                              const rs::world::Guid& selectedBlock);

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

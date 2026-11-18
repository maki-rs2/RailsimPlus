//
// EdgeSplitMergeCommand.cpp - SplitEdgeCommand の実装(MergeEdgeCommand は EdgeMergeCommand.cpp)
//
#include "EdgeSplitMergeCommand.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/AutoCant.h"
#include "../../world/alignment/BlockRegistry.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::ManualPlacementOverlay;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;

SplitEdgeCommand::SplitEdgeCommand(Guid edgeGuid, double s, ManualPlacementOverlay& overlay)
    : edgeGuid_(edgeGuid),
      s_(s),
      overlay_(overlay),
      newEdgeGuid_(rs::world::GenerateGuid()),
      newNodeGuid_(rs::world::GenerateGuid()) {}

void SplitEdgeCommand::Apply(TrackGraph& graph) {
    applied_ = false;
    savedReferences_.reset();

    auto it = graph.edges.find(edgeGuid_);
    if (it == graph.edges.end()) return;
    const TrackEdge parentEdge = it->second;  // コピー(下でnode/edgeを書き換えるため)

    std::optional<rs::world::EdgeSplitResult> result =
        rs::world::BuildEdgeSplit(graph, parentEdge, s_, newEdgeGuid_, newNodeGuid_);
    if (!result) return;

    // Revert用スナップショットを保存。
    savedParentEdge_ = parentEdge;
    auto itA = graph.nodes.find(parentEdge.nodeA);
    hadNodeA_ = itA != graph.nodes.end();
    if (hadNodeA_) savedNodeA_ = itA->second;
    auto itB = graph.nodes.find(parentEdge.nodeB);
    hadNodeB_ = itB != graph.nodes.end();
    if (hadNodeB_) savedNodeB_ = itB->second;


    TrackEdge first = std::move(result->first);
    TrackEdge second = std::move(result->second);
    TrackNode middle = std::move(result->middle);

    // middle nodeのlinksを構築(前半side0/後半side1)。
    middle.links = {
        TrackNode::Link{first.guid, 1, 0},
        TrackNode::Link{second.guid, 0, 1},
    };
    middle.activeSlot = {0, 0};

    rs::world::TrackGraphReferenceChange change;
    change.kind = rs::world::TrackGraphReferenceChange::Kind::Split;
    change.firstEdge = edgeGuid_;
    change.secondEdge = newEdgeGuid_;
    change.splitS = s_;
    savedReferences_ = rs::world::RemapTrackGraphReferences(graph, change);

    graph.nodes[middle.guid] = middle;
    graph.edges[first.guid] = first;
    graph.edges[second.guid] = second;

    // 両端ノードの姿勢から形を作り直し、実長でRescaleEdgeSections(順序が重要)。
    // 形は BuildEdgeSplit の事前確認(CheckEdgeSplit)と同じ入力で作るため解けるはずだが、
    // 解けなかったときは何も変えずに戻す。
    const double parentTotalLen =
        parentEdge.spline.empty() ? 0.0 : static_cast<double>(parentEdge.spline.back().s);
    const std::pair<Guid, double> halves[2] = {{first.guid, s_}, {second.guid, parentTotalLen - s_}};
    for (const std::pair<Guid, double>& half : halves) {
        TrackEdge& halfEdge = graph.edges.at(half.first);
        if (!rs::world::RebuildEdgeSpline(graph, halfEdge)) {
            spdlog::warn("[edge] 分割後の形を作れないため分割を取り消し");
            RestoreSplitGraph(graph);
            return;
        }
        const double actualLen = static_cast<double>(halfEdge.spline.back().s);
        rs::world::RescaleEdgeSections(halfEdge, half.second, actualLen);
    }
    //自動カントの前半・後半と両外側の隣は、お互いの curve が出そろってからつなぎ目を決め直す。
    rs::world::RebuildAutoCantAround(graph, {first.guid, second.guid});

    // overlay remap: 親guidを指しs>=splitSのものを新edgeへ再アンカーする。
    // 実体化済み個体(PlacedIndividual)も同じ規約でremapする。lines(WireSpan)は
    // 個体のGuidを参照するだけなので、個体側のedge/sを直せば架線も追従する。
    for (rs::world::PlacedIndividual& individual : overlay_.poles) {
        if (individual.edge == edgeGuid_ && individual.s >= s_) {
            individual.edge = newEdgeGuid_;
            individual.s -= s_;
        }
    }
    for (rs::world::PlacedIndividual& individual : overlay_.piers) {
        if (individual.edge == edgeGuid_ && individual.s >= s_) {
            individual.edge = newEdgeGuid_;
            individual.s -= s_;
        }
    }
    // 実体化済みfolderの記録を新edge側にも複製する。複製しないと後半側は
    // 「未実体化」に見え、SetSectionFolders越しに同じfolderを再投入できてしまう
    // (実体化済み個体と自動生成が二重に出る不正状態)。
    // carry(下流へ渡す位相アンカー)は分割点より下流の後半側が引き継ぐ性質のため、
    // 新edge側だけ複製し、手前側(edgeGuid_、既存の記録)は変更しない。
    // 既知の制限: hasAnchor==trueの記録を分割すると、複製側はcarryを持たない
    // (=分割点をrun終端として扱わない)ため、真の終端まで届かない。稀にしか起きない
    // (.rs2取り込みは常にhasAnchor==false)ため許容し、修正は追って行う。
    std::vector<rs::world::RunAnchor> duplicatedSections;
    for (const rs::world::RunAnchor& section : overlay_.runAnchors) {
        if (section.edge != edgeGuid_) continue;
        rs::world::RunAnchor copy;
        copy.edge = newEdgeGuid_;
        copy.folderUtf8 = section.folderUtf8;
        copy.hasAnchor = false;
        copy.anchorS = 0.0;
        duplicatedSections.push_back(copy);
    }
    overlay_.runAnchors.insert(overlay_.runAnchors.end(), duplicatedSections.begin(),
                             duplicatedSections.end());

    applied_ = true;
}

void SplitEdgeCommand::Revert(TrackGraph& graph) {
    if (!applied_) return;

    RestoreSplitGraph(graph);

    // overlayはスナップショット復元ではなく、Applyが行ったremapの逆変換だけを適用する。
    // 配置編集はCommandStackを経由せず
    // overlayを直接書き換えるため、丸ごと復元すると分割以降に行った編集まで消える。
    for (rs::world::PlacedIndividual& individual : overlay_.poles)
        UnremapSplitAnchor(individual.edge, individual.s);
    for (rs::world::PlacedIndividual& individual : overlay_.piers)
        UnremapSplitAnchor(individual.edge, individual.s);
    // newEdgeGuid_はこのコマンドがコンストラクタで新規発番したguidなので、そこを
    // 指すrunAnchorsは全てApplyで複製した側だと確定できる(元から存在した記録では
    // ない)。よって単純に消すだけで良い(元のedgeGuid_側は変更していないため復元不要)。
    overlay_.runAnchors.erase(
        std::remove_if(overlay_.runAnchors.begin(), overlay_.runAnchors.end(),
                       [this](const rs::world::RunAnchor& section) {
                           return section.edge == newEdgeGuid_;
                       }),
        overlay_.runAnchors.end());
    applied_ = false;
}

void SplitEdgeCommand::RestoreSplitGraph(TrackGraph& graph) {
    if (savedReferences_) rs::world::RestoreTrackGraphReferences(graph, *savedReferences_);

    graph.nodes.erase(newNodeGuid_);
    graph.edges.erase(newEdgeGuid_);
    graph.edges[edgeGuid_] = savedParentEdge_;

    if (hadNodeA_) graph.nodes[savedNodeA_.guid] = savedNodeA_;
    if (hadNodeB_) graph.nodes[savedNodeB_.guid] = savedNodeB_;
    rs::world::RebuildAutoCantAround(graph, {edgeGuid_});
}

void SplitEdgeCommand::UnremapSplitAnchor(Guid& edge, double& s) const {
    if (edge != newEdgeGuid_) return;
    edge = edgeGuid_;
    s += s_;
}

}  // namespace rs::editor

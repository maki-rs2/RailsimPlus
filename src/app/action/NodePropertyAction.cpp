//
// NodePropertyAction.cpp - 選択nodeプロパティのアプリサービス
//
#include "NodePropertyAction.h"
#include "../../editor/tool/Editor.h"

#include <memory>
#include <optional>

#include "../../editor/tool/Command.h"
#include "../plugin_session/LayoutSession.h"

namespace rs::app {

NodePropertyAction::NodePropertyAction(rs::editor::Editor& editor,
                                       rs::editor::WarpLinkAuthoring& warpAuthoring,
                                       LayoutSession& layoutSession)
    : editor_(editor), warpAuthoring_(warpAuthoring), layoutSession_(layoutSession) {}


void NodePropertyAction::BeginWarpPending() {
    warpAuthoring_.BeginPending(layoutSession_.activeIndex, editor_.selectedNode);
}

bool NodePropertyAction::ConfirmWarpPending() {
    return warpAuthoring_.ConfirmPending(layoutSession_.activeIndex, editor_.selectedNode, editor_, layoutSession_.warpLinks);
}

void NodePropertyAction::AutoConfirmWarpPending() {
    if (!warpAuthoring_.HasPending()) return;
    if (editor_.graph.nodes.find(editor_.selectedNode) == editor_.graph.nodes.end()) return;
    // 保留中の端点そのもの(保留した直後の選択)はConfirmPendingが false で返し、保留を残す。
    ConfirmWarpPending();
}

void NodePropertyAction::CancelWarpPending() {
    warpAuthoring_.CancelPending();
}

void NodePropertyAction::RemoveWarp() {
    const rs::world::WarpEndpoint self{layoutSession_.activeIndex, editor_.selectedNode};
    const std::optional<rs::world::WarpEndpoint> other =
        layoutSession_.warpLinks.Find(self.sceneIndex, self.node);
    if (!other.has_value()) return;
    editor_.ExecuteCommand( std::make_unique<rs::editor::RemoveWarpLinkCommand>(
                                             layoutSession_.warpLinks, self, *other));
}

void NodePropertyAction::MergeEdges(bool allowLayerMismatch) {
    editor_.MergeEdgeAt(editor_.selectedNode, &layoutSession_.warpLinks, allowLayerMismatch);
}

NodePropertyViewModel NodePropertyAction::ViewModel() {
    NodePropertyViewModel viewModel;
    const auto nodeIt = editor_.graph.nodes.find(editor_.selectedNode);
    if (nodeIt == editor_.graph.nodes.end()) {
        editor_.ClearSelectedNode();
        return viewModel;
    }

    const rs::world::TrackNode& node = nodeIt->second;
    viewModel.visible = true;
    viewModel.elevation = node.pos.y;
    viewModel.linkCount = node.links.size();
    viewModel.hasWarpPending = warpAuthoring_.HasPending();
    const std::optional<rs::world::WarpEndpoint> warpPartner =
        layoutSession_.warpLinks.Find(layoutSession_.activeIndex, editor_.selectedNode);
    viewModel.hasWarpLink = warpPartner.has_value();
    if (warpPartner.has_value()) viewModel.warpPartnerScene = warpPartner->sceneIndex;
    // warpLinksは必ず実テーブルを渡す(nullptrで呼ぶとWarpOnSharedNode判定が素通りする既知の罠)。
    viewModel.mergeRefusal =
        rs::world::CheckEdgeMerge(editor_.graph, editor_.selectedNode, &layoutSession_.warpLinks);
    // LayerMismatchのみが拒否理由かどうかを、allowLayerMismatch=trueで再判定して確認する。
    // 他の拒否理由が無くLayerMismatchが解消すればNoneになるはず。
    if (viewModel.mergeRefusal == rs::world::EdgeMergeRefusal::LayerMismatch) {
        const rs::world::EdgeMergeRefusal refusalWithoutLayerCheck = rs::world::CheckEdgeMerge(
            editor_.graph, editor_.selectedNode, &layoutSession_.warpLinks,
            /*allowLayerMismatch=*/true);
        if (refusalWithoutLayerCheck == rs::world::EdgeMergeRefusal::None) {
            viewModel.mergeBlockedOnlyByLayerMismatch = true;
            if (node.links.size() == 2) {
                const auto edgeFirstIt = editor_.graph.edges.find(node.links[0].edge);
                const auto edgeSecondIt = editor_.graph.edges.find(node.links[1].edge);
                if (edgeFirstIt != editor_.graph.edges.end())
                    viewModel.mergeEdgeFirstLayer = edgeFirstIt->second.layer;
                if (edgeSecondIt != editor_.graph.edges.end())
                    viewModel.mergeEdgeSecondLayer = edgeSecondIt->second.layer;
            }
        }
    }
    return viewModel;
}

} // namespace rs::app

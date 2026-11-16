//
// EditorEditOps.cpp - 選択edge/nodeの編集操作(editor.cppから分離)
//
// 選択中の要素へのプロパティ変更・削除・標高編集。いずれもCommandStack
// 経由でグラフを変更しUndoに載せる。Editorクラスのメソッド実装を担う翻訳単位。
//
#include "../../core/Epsilon.h"
#include "Editor.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "EdgeSplitMergeCommand.h"
#include "EditorCenterlineMerge.h"
#include "EditorCenterlineSplit.h"
#include "BlockSignalCleanupCommand.h"
#include "PlacementEditCommand.h"
#include "PlacementOverlayCommand.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;

namespace {

bool SameWarpEndpoint(const rs::world::WarpEndpoint& a, const rs::world::WarpEndpoint& b) {
    return a.sceneIndex == b.sceneIndex && a.node == b.node;
}

bool ContainsWarpLink(const std::vector<rs::world::WarpLink>& links,
                      const rs::world::WarpEndpoint& a, const rs::world::WarpEndpoint& b) {
    for (const rs::world::WarpLink& link : links) {
        if ((SameWarpEndpoint(link.a, a) && SameWarpEndpoint(link.b, b)) ||
            (SameWarpEndpoint(link.a, b) && SameWarpEndpoint(link.b, a)))
            return true;
    }
    return false;
}

void AddRemovedWarpLinkCommands(const rs::world::TrackGraph& graph,
                                const std::vector<Guid>& removedEdges,
                                const std::vector<Guid>& candidateNodes,
                                rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex,
                                CompositeCommand& composite) {
    if (warpLinks == nullptr) return;

    std::vector<rs::world::WarpLink> linksToRemove;
    std::vector<Guid> inspectedNodes;
    for (const Guid& nodeGuid : candidateNodes) {
        if (std::find(inspectedNodes.begin(), inspectedNodes.end(), nodeGuid) != inspectedNodes.end())
            continue;
        inspectedNodes.push_back(nodeGuid);
        const auto nodeIt = graph.nodes.find(nodeGuid);
        if (nodeIt == graph.nodes.end()) continue;

        bool hasRemainingLink = false;
        for (const rs::world::TrackNode::Link& link : nodeIt->second.links) {
            if (std::find(removedEdges.begin(), removedEdges.end(), link.edge) == removedEdges.end()) {
                hasRemainingLink = true;
                break;
            }
        }
        if (hasRemainingLink) continue;

        const std::optional<rs::world::WarpEndpoint> other = warpLinks->Find(sceneIndex, nodeGuid);
        if (!other.has_value()) continue;

        const rs::world::WarpEndpoint endpoint{sceneIndex, nodeGuid};
        if (ContainsWarpLink(linksToRemove, endpoint, *other)) continue;
        linksToRemove.push_back(rs::world::WarpLink{endpoint, *other});
        composite.Add(std::make_unique<RemoveWarpLinkCommand>(*warpLinks, endpoint, *other));
    }
}

} // namespace

// 実体化と編集を1コマンドとして積む。no-opの要求は積まずに捨てる(積むと動かさずに
// クリックしただけでUndo項目が増え、revision更新でメッシュ全再構築まで走る)。
bool Editor::ExecutePlacementEdit(std::vector<rs::world::MaterializeSnapshot> snapshots,
                                  PlacementEditCommand::Request request) {
    std::unique_ptr<PlacementEditCommand> command = std::make_unique<PlacementEditCommand>(
        manualOverlay, std::move(snapshots), std::move(request));
    if (!command->WouldChange(graph)) return false;
    return ExecuteCommand(std::move(command));
}

void Editor::DeleteSelectedPlacement(std::vector<rs::world::MaterializeSnapshot> snapshots) {
    if (!HasSelectedPlacement()) return;
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::Delete;
    request.edge = selectedPlacementEdge;
    request.s = selectedPlacementS;
    request.folderUtf8 = selectedPlacementFolder;
    if (!ExecutePlacementEdit(std::move(snapshots), std::move(request))) return;
    NoteRemoved();
    ClearSelectedPlacement();
}

bool Editor::DematerializeSelectedEdgeFolder(const std::string& folderUtf8) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::Dematerialize;
    request.edge = selectedEdge;
    request.folderUtf8 = folderUtf8;
    return ExecutePlacementEdit({}, std::move(request));
}

void Editor::AddManualPlacement(const Guid& edge, double s, const std::string& folderUtf8,
                                bool keepUpright, float verticalOffsetM,
                                std::vector<rs::world::MaterializeSnapshot> snapshots) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::Add;
    request.edge = edge;
    request.s = s;
    request.folderUtf8 = folderUtf8;
    request.keepUpright = keepUpright;
    request.verticalOffsetM = verticalOffsetM;
    if (ExecutePlacementEdit(std::move(snapshots), std::move(request))) NotePlaced();
}

void Editor::MovePlacementTo(const Guid& edge, double oldS, const std::string& folderUtf8,
                             double newS, bool keepUpright, float verticalOffsetM,
                             float oldLateralOffsetM, float newLateralOffsetM,
                             std::vector<rs::world::MaterializeSnapshot> snapshots) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::Move;
    request.edge = edge;
    request.s = oldS;
    request.folderUtf8 = folderUtf8;
    request.newEdge = edge;
    request.newS = newS;
    request.keepUpright = keepUpright;
    request.verticalOffsetM = verticalOffsetM;
    request.oldLateralOffsetM = oldLateralOffsetM;
    request.newLateralOffsetM = newLateralOffsetM;
    ExecutePlacementEdit(std::move(snapshots), std::move(request));
}

void Editor::MovePlacementTo(const Guid& oldEdge, double oldS, const std::string& folderUtf8,
                             const Guid& newEdge, double newS, bool keepUpright,
                             float verticalOffsetM, float oldLateralOffsetM,
                             float newLateralOffsetM, int newDir,
                             std::vector<rs::world::MaterializeSnapshot> snapshots) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::Move;
    request.edge = oldEdge;
    request.s = oldS;
    request.folderUtf8 = folderUtf8;
    request.newEdge = newEdge;
    request.newS = newS;
    request.keepUpright = keepUpright;
    request.verticalOffsetM = verticalOffsetM;
    request.oldLateralOffsetM = oldLateralOffsetM;
    request.newLateralOffsetM = newLateralOffsetM;
    request.newDir = newDir;
    ExecutePlacementEdit(std::move(snapshots), std::move(request));
}

bool Editor::ConnectWireSpan(const WirePointRef& pointA, const WirePointRef& pointB,
                             const std::string& lineFolderUtf8,
                             std::vector<rs::world::MaterializeSnapshot> snapshots) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::ConnectWire;
    request.wirePointA = pointA;
    request.wirePointB = pointB;
    request.lineFolderUtf8 = lineFolderUtf8;
    return ExecutePlacementEdit(std::move(snapshots), std::move(request));
}

bool Editor::DisconnectWireSpan(const WirePointRef& pointA, const WirePointRef& pointB,
                                std::vector<rs::world::MaterializeSnapshot> snapshots) {
    PlacementEditCommand::Request request;
    request.kind = PlacementEditCommand::Kind::DisconnectWire;
    request.wirePointA = pointA;
    request.wirePointB = pointB;
    return ExecutePlacementEdit(std::move(snapshots), std::move(request));
}

void Editor::RemoveSelectedEdge(rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    // 選択中のレール全部を1回のUndoで消す。
    std::vector<Guid> edgesToRemove;
    std::vector<Guid> candidateNodes;
    for (const Guid& guid : SelectedEdges()) {
        const auto it = graph.edges.find(guid);
        if (it == graph.edges.end()) continue;
        edgesToRemove.push_back(guid);
        candidateNodes.push_back(it->second.nodeA);
        candidateNodes.push_back(it->second.nodeB);
    }
    if (edgesToRemove.empty()) return;

    CompositeCommand composite;
    for (const Guid& guid : edgesToRemove) composite.Add(std::make_unique<RemoveEdgeCommand>(guid));
    AddRemovedWarpLinkCommands(graph, edgesToRemove, candidateNodes, warpLinks, sceneIndex,
                               composite);
    // 複線中心線(centerlineOnly)を削除すると複線一括Pier/Poleの自動配置は
    // routing先(CenterlineChainsFor)を失い再導出時に自然に消えるが、その edge を
    // アンカーにした実体化済み配置は参照先を失い孤立するため併せて消す(実レールedge削除でも同様の孤立が
    // 起き得るため edge種別を問わず行う)。
    // 除去はコマンド経由で行う: CommandStackの外で消すとUndoしても戻らない。
    composite.Add(std::make_unique<ClearPlacementEditsCommand>(manualOverlay, edgesToRemove));
    if (ExecuteCommand(std::make_unique<CompositeCommand>(std::move(composite)))) NoteRemoved();
    ClearSelectedEdge();
}

void Editor::RemoveSelectedNode(rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    if (!(selectedNode != NilGuid())) return;
    auto it = graph.nodes.find(selectedNode);
    if (it == graph.nodes.end()) { selectedNode = NilGuid(); return; }

    // selectedNode自体が最初から接続edgeを持たない孤立nodeだったかどうか。
    // この場合はedgeGuidsが空になりRemoveEdgeCommandは1つも積まれないため、
    // 別途EraseOrphanNodeCommandでコマンドスタックへ乗せる(Undo対象にするため)。
    const bool wasIsolatedNode = it->second.links.empty();

    // 接続edgeのGuidを保存してから削除する（削除中にlinksが変わるため）。
    // RemoveEdgeCommandは端点nodeが孤立(links空)になると自動でnodeも削除するので、
    // 最後の接続edgeを消した時点でそのnode自身も取り除かれる。
    // 同じedgeへlinkが2本刺さっている(環状)構成があるため重複は除く。
    std::vector<Guid> edgeGuids;
    std::vector<Guid> candidateNodes{selectedNode};
    edgeGuids.reserve(it->second.links.size());
    for (const auto& link : it->second.links) {
        if (graph.edges.find(link.edge) == graph.edges.end()) continue;
        if (std::find(edgeGuids.begin(), edgeGuids.end(), link.edge) != edgeGuids.end()) continue;
        edgeGuids.push_back(link.edge);
        const TrackEdge& edge = graph.edges.at(link.edge);
        candidateNodes.push_back(edge.nodeA);
        candidateNodes.push_back(edge.nodeB);
    }

    // 「node1個の削除」はユーザーから見て1操作なので、接続edgeが何本あっても
    // Undo1回で戻せる単一のCompositeCommandにまとめる。
    CompositeCommand composite;
    for (const Guid& eg : edgeGuids) {
        composite.Add(std::make_unique<RemoveEdgeCommand>(eg));
    }
    AddRemovedWarpLinkCommands(graph, edgeGuids, candidateNodes, warpLinks, sceneIndex, composite);
    // RemoveSelectedEdge同様、孤立する個別編集をコマンド経由で除去する。
    if (!edgeGuids.empty()) {
        composite.Add(std::make_unique<ClearPlacementEditsCommand>(manualOverlay, edgeGuids));
    }
    if (wasIsolatedNode) {
        composite.Add(std::make_unique<EraseOrphanNodeCommand>(selectedNode));
    }
    if (!composite.Empty() && ExecuteCommand(std::make_unique<CompositeCommand>(std::move(composite))))
        NoteRemoved();
    PruneSelectedEdges();

    selectedNode = NilGuid();
}

bool Editor::SplitEdgeAt(const Guid& edgeGuid, double s) {
    const auto it = graph.edges.find(edgeGuid);
    if (it == graph.edges.end()) return false;

    // 複線中心線グループ(中心線edge自身、または実レールmember)に属する場合は、
    // グループ全体を同期分割する経路へ委譲する。
    const Guid centerlineGuid = FindOwningCenterlineGroup(graph, edgeGuid);
    if (centerlineGuid != NilGuid()) {
        double sCenter = s;
        if (!(edgeGuid == centerlineGuid)) {
            // クリックされたのは実レールmember: そのedge自身の弧長比で中心線側の
            // sへ変換する(OffsetCurve既存近似と同じ精度規約)。
            const auto centerIt = graph.edges.find(centerlineGuid);
            if (centerIt == graph.edges.end()) return false;
            if (it->second.spline.empty() || centerIt->second.spline.empty()) return false;
            const double lenMember = static_cast<double>(it->second.spline.back().s);
            const double lenCenter = static_cast<double>(centerIt->second.spline.back().s);
            if (lenMember <= rs::core::kDegenerateVectorEps) return false;
            sCenter = s * (lenCenter / lenMember);
        }
        return SplitCenterlineGroupAt(*this, centerlineGuid, sCenter);
    }

    if (rs::world::CheckEdgeSplit(graph, it->second, s) != rs::world::EdgeSplitRefusal::None)
        return false;

    if (!ExecuteCommand(std::make_unique<SplitEdgeCommand>(edgeGuid, s, manualOverlay))) return false;

    // selectedEdgeが既にedgeGuidの場合SelectEdge()自体はガード(edge不変)で添字を
    // リセットしないため、分割後は明示的にリセットする(区間選択添字は
    // 前のedgeの区間を指したまま古くなるため)。
    SelectEdge(edgeGuid);
    ResetSelectedSectionIndices();

    return true;
}

std::optional<rs::world::EdgeMergeRemap> Editor::MergeEdgeAt(
    const Guid& sharedNodeGuid, const rs::world::WarpLinkTable* warpLinks,
    bool allowLayerMismatch) {
    // 複線中心線グループ(中心線edge自身、または実レールmember)に属する場合は、
    // グループ全体を同期結合する経路へ委譲する。
    if (IsCenterlineGroupMergeNode(graph, sharedNodeGuid))
        return SyncMergeCenterlineGroupAt(*this, sharedNodeGuid, warpLinks, allowLayerMismatch);

    // warpLinksがnullptrの場合はワープリンク判定をスキップする(呼び出し側が
    // WarpLinkTableを渡せない未配線経路向けの既知の制限、Editor.h参照)。
    if (rs::world::CheckEdgeMerge(graph, sharedNodeGuid, warpLinks, allowLayerMismatch) !=
        rs::world::EdgeMergeRefusal::None)
        return std::nullopt;

    std::unique_ptr<MergeEdgeCommand> cmd = std::make_unique<MergeEdgeCommand>(
        sharedNodeGuid, manualOverlay, warpLinks, allowLayerMismatch);
    MergeEdgeCommand* cmdPtr = cmd.get();
    const std::vector<Guid> selectionBefore = SelectedEdges();
    if (!ExecuteCommand(std::move(cmd))) return std::nullopt;

    const rs::world::EdgeMergeRemap remap = *cmdPtr->MergeRemap();

    // 選択中のedgeが結合対象だった場合、結合後に残るedgeへ選択を寄せる。
    SelectEdges(selectionBefore);
    ReplaceSelectedEdge(remap.removedEdge, remap.keptEdge);
    PruneSelectedEdges();
    if (selectedEdge == remap.keptEdge) ResetSelectedSectionIndices();

    return remap;
}

} // namespace rs::editor

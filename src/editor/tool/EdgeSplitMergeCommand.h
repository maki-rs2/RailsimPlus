//
// EdgeSplitMergeCommand.h - edge分割(SplitEdge)のUndo可能コマンド
//
// Command.cpp が既に400行目安に達しているため、分割・結合系コマンドはこちらへ
// 分離する。
//
#pragma once

#include <vector>

#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"
#include "../../world/alignment/TrackGraphReferences.h"

namespace rs::editor {

// edgeを弧長sで2本へ分割する。newEdgeGuid/newNodeGuidはコンストラクタで1度だけ
// 発番して保持する(Undo/Redoで採番し直すと、overlayや他edgeからの参照が切れる)。
// CheckEdgeSplitでの拒否/BuildEdgeSplitの失敗時はApplyが何もしない
// (Applied()==false)。
class SplitEdgeCommand : public ICommand {
public:
    SplitEdgeCommand(rs::world::Guid edgeGuid, double s,
                      rs::world::ManualPlacementOverlay& overlay);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

    // Applyが実際に分割を行ったか(拒否条件に該当した場合はfalse)。
    bool Applied() const { return applied_; }

    // Applyで新設されたnode/edgeのguid(コンストラクタで発番済み、Apply前後で不変)。
    // 呼び出し側がこのnodeを起点として後続のAddEdgeCommandを組み立てるために使う。
    rs::world::Guid NewNodeGuid() const { return newNodeGuid_; }
    rs::world::Guid NewEdgeGuid() const { return newEdgeGuid_; }

private:
    rs::world::Guid edgeGuid_;
    double s_;
    rs::world::ManualPlacementOverlay& overlay_;
    rs::world::Guid newEdgeGuid_;
    rs::world::Guid newNodeGuid_;

    bool applied_ = false;

    // Applyが行ったoverlay remapの逆変換(新edge側のアンカーを親edgeへ戻す)。
    // overlayを丸ごとスナップショット復元すると、分割以降にCommandStackを経由せず
    // 行われた配置編集まで巻き戻してしまう。
    void UnremapSplitAnchor(rs::world::Guid& edge, double& s) const;

    // Applyで保存したedge/ノード/閉塞/信号を戻す(Revertと、形を作れなかったときの取り消し)。
    void RestoreSplitGraph(rs::world::TrackGraph& graph);

    // Revert用スナップショット(Apply時に保存)。overlayは含めない(上記)。
    rs::world::TrackEdge savedParentEdge_;
    bool hadNodeA_ = false;
    rs::world::TrackNode savedNodeA_;
    bool hadNodeB_ = false;
    rs::world::TrackNode savedNodeB_;
    std::optional<rs::world::TrackGraphReferenceSnapshot> savedReferences_;
};

// 共有node(次数2)を境に2本のedgeを1本へ結合する。
// 結合後のedgeは前半役(edgeFirst、共有nodeのlinks[0]が指す方)の
// guidを再利用する(remap範囲を半分にするため)。CheckEdgeMerge/BuildEdgeMergeの
// 失敗時はApplyが何もしない(Applied()==false)。
// warpLinksはnullable(nullptrならワープ判定をスキップする、呼び出し側にWarpLinkTable
// が無い場合の既知の制限)。
class MergeEdgeCommand : public ICommand {
public:
    // allowLayerMismatch=trueの場合、layer不一致による拒否をスキップする(UI確認ダイアログ
    // で許可済みのケース)。
    MergeEdgeCommand(rs::world::Guid sharedNodeGuid, rs::world::ManualPlacementOverlay& overlay,
                      const rs::world::WarpLinkTable* warpLinks,
                      bool allowLayerMismatch = false);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

    // Applyが実際に結合を行ったか(拒否条件に該当した場合はfalse)。
    bool Applied() const { return applied_; }

    // 結合のremap記述子。一度も結合が成立していなければnullopt。
    std::optional<rs::world::EdgeMergeRemap> MergeRemap() const {
        if (!hasRemap_) return std::nullopt;
        return remap_;
    }

private:
    void RewireMergedGraph(rs::world::TrackGraph& graph,
                           const rs::world::EdgeMergeResult& result,
                           rs::world::Guid edgeFirstGuid, rs::world::Guid edgeSecondGuid);
    // 遠端2ノードの姿勢から結合後の形を作り、区間を実長へ合わせる。解けなければfalse。
    bool RebuildMergedGeometry(rs::world::TrackGraph& graph, rs::world::Guid edgeFirstGuid,
                               double assumedLen);
    // Applyで保存したedge/ノード/閉塞/信号を戻す(Revertと、形を作れなかったときの取り消し)。
    void RestoreMergedGraph(rs::world::TrackGraph& graph);
    void RemapOverlayForMerge();

    rs::world::Guid sharedNodeGuid_;
    rs::world::ManualPlacementOverlay& overlay_;
    const rs::world::WarpLinkTable* warpLinks_;
    bool allowLayerMismatch_ = false;

    bool applied_ = false;
    rs::world::Guid keptEdgeGuid_ = rs::world::NilGuid();
    rs::world::Guid removedEdgeGuid_ = rs::world::NilGuid();
    rs::world::Guid removedNodeGuid_ = rs::world::NilGuid();
    rs::world::EdgeMergeRemap remap_;
    bool hasRemap_ = false;

    // Revert用スナップショット(Apply時に保存)。overlayは含めず、Revertでは
    // UnremapMergedAnchorによる逆remapだけを行う(SplitEdgeCommandと同じ理由)。
    rs::world::TrackEdge savedEdgeFirst_;
    rs::world::TrackEdge savedEdgeSecond_;
    rs::world::TrackNode savedSharedNode_;
    bool hadFrontFarNode_ = false;
    rs::world::TrackNode savedFrontFarNode_;
    bool hadBackFarNode_ = false;
    rs::world::TrackNode savedBackFarNode_;
    std::optional<rs::world::TrackGraphReferenceSnapshot> savedReferences_;

    // overlay.runAnchorsのremapで保存する情報(Revert用)。runAnchorsはedge単位の記録で、
    // removedEdge側とkeptEdge側で同じfolderが重複するとremovedEdge側を消して1件に
    // まとめるため、単純なUnremapMergedAnchorだけでは戻せない。
    std::vector<rs::world::RunAnchor> droppedSections_;  // 消した側(Revertで復元)
    std::vector<std::string> remappedSectionFolders_;  // keptEdgeへ付け替えた側(Revertで戻す)
};

}  // namespace rs::editor

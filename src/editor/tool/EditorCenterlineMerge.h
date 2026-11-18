//
// EditorCenterlineMerge.h - 複線中心線グループのedge結合トポロジ追従
//
// member実レールedge(または中心線edge自身)の結合を、同じグループの全N本の実レール+
// 中心線edgeへ対応する位置で同期させ、1つのUndo単位(CompositeCommand)にまとめる。
//
#pragma once

#include <optional>

#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

class Editor;

// sharedNodeGuidの2本のlink edge(共有node.links[0]/[1])のいずれかが複線中心線グループ
// (centerlineOnly edge自身、またはtrackEdgesメンバー)に絡む場合はtrue。falseなら
// 呼び出し側は通常の単一edge結合(Editor::MergeEdgeAtの既定経路)を使う。
bool IsCenterlineGroupMergeNode(const rs::world::TrackGraph& graph,
                                 const rs::world::Guid& sharedNodeGuid);

// sharedNodeGuidを起点に、複線中心線グループ全体(中心線+全member)を同期結合する。
// IsCenterlineGroupMergeNodeがtrueの場合のみ呼ぶ前提(falseの場合の動作は未定義)。
// 同期結合できない場合(位相不整合・CheckEdgeMerge拒否等)はnulloptを返し、何も
// 変更しない(部分適用を避けるため、通常の単一edge結合へはフォールバックしない。
// 呼び出し側もnulloptならそこで諦めること)。成功時は1つのCompositeCommandを
// ExecuteCommandし、中心線の結合後guidに対応するEdgeMergeRemapを返す。
// allowLayerMismatch=trueの場合、グループ内の全nodeについてlayer不一致による拒否を
// スキップする(UI確認ダイアログで許可済みのケース。グループ全体へ一括適用し、
// member単位でlayerの許可/拒否は分けない)。
std::optional<rs::world::EdgeMergeRemap> SyncMergeCenterlineGroupAt(
    Editor& ed, const rs::world::Guid& sharedNodeGuid,
    const rs::world::WarpLinkTable* warpLinks, bool allowLayerMismatch = false);

} // namespace rs::editor

//
// EditorNodeDrag.h - ノード/中心線ノードのドラッグ移動・接線編集(計画の実行とプレビュー)
//
// Editor を明示引数で受け取る無状態の操作クラス
// (ドラッグ中のプレビュー状態はUI所有のためEditor側に残る、trackCount等と
// 同じ規約)。姿勢の決め方と事前確認は NodeDragPlan.h の計画関数に1か所だけ置き、
// ここは計画が ok のときだけ MoveNodeCommand の CompositeCommand として実行する薄い層。
// プレビューも同じ計画の edges(BuildEdgeShape の spline)を返す。
//
#pragma once

#include "../../core/Math.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"
#include "NodeDragPlan.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

class Editor;

class EditorNodeDrag {
public:
    // 通常ノード(中心線につながらず、links が1以上)を動かせるか。つながる全edgeの形を
    // 作れるかは動かすときの計画で確かめる(作れなければ MoveNodeTo が false)。
    bool CanMoveNode(const rs::world::TrackGraph& graph, const rs::world::Guid& nodeGuid) const;

    // 中心線(centerlineOnly)ノードをメンバーごとドラッグできるか。条件は NodeDragCenterline.h。
    bool CanMoveCenterlineNode(const rs::world::TrackGraph& graph,
                               const rs::world::Guid& nodeGuid) const;

    // 以下3つの実行は、edge の長さが変わった分だけ overlay の配置物の位置を比例させる
    // (PlacementRescaleCommand.h)。
    // nodeGuid を newPos(高さ込み)へ移動する(PlanNodeMove を実行)。動かせない、または
    // 形を作れないedgeがあるなら何も変更せず false(1操作=1Undo単位、部分適用はしない)。
    bool MoveNodeTo(Editor& editor,
                    rs::world::ManualPlacementOverlay& overlay,
                    const rs::world::Guid& nodeGuid, const rs::core::Vec3& newPos);

    // nodeGuid の標高だけを変える(PlanNodeElevation を実行)。標高が同じ、または形を作れない
    // edgeがあるなら何も変更せず false。複線中心線のノードはメンバーも追従する。
    bool SetNodeElevation(Editor& editor,
                          rs::world::ManualPlacementOverlay& overlay,
                          const rs::world::Guid& nodeGuid, float elevation);

    // 接線を newTangent へ直接設定する(接線ハンドルのドラッグ確定)。
    // 位置は変えない。次数0または3以上、形を作れない場合は false。
    bool SetNodeTangent(Editor& editor,
                          rs::world::ManualPlacementOverlay& overlay,
                          const rs::world::Guid& nodeGuid, const rs::core::Vec3& newTangent) const;

    // ---- ドラッグ中プレビュー(dry-run。計画をそのまま返す。何も変更しない) ----
    // ok==false なら edges は空(プレビューを描かず、ガイドをエラー色にする)。
    NodeDragPlan PreviewMoveNodeTo(const rs::world::TrackGraph& graph,
                                   const rs::world::Guid& nodeGuid,
                                   const rs::core::Vec3& candidatePos) const;
    NodeDragPlan PreviewSetNodeTangent(const rs::world::TrackGraph& graph,
                                         const rs::world::Guid& nodeGuid,
                                         const rs::core::Vec3& candidateTangent) const;
};

} // namespace rs::editor

//
// EditorEdgeProperties.h - 選択edge/nodeへのプロパティ変更操作
//
// Editor と対象の選択guidを明示引数で受け取る無状態の
// 操作クラス(選択状態そのものはUI/描画が参照するためEditor側に残る、
// EditorNodeDragと同じ規約)。manualOverlay等の配置オーバーレイ層は扱わない
// (RemoveSelectedEdge/RemoveSelectedNodeがそちらへも跨るオーケストレーション
// のため、意図的にEditor本体へ残してある)。
//
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"
#include "../../world/alignment/AlignmentSegment.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackEdge;
} // namespace rs::world

namespace rs::editor {

class Editor;

class EditorEdgeProperties {
public:
    // selectedEdgeへdeltaを適用する(Undo対応)。selectedEdgeがNilGuid、または
    // graphに存在しなければ何もせずfalseを返す(実行後の世代更新は共通の入口が行う)。
    // overlay が非 null なら、edge の長さが変わった分だけ配置物の位置を比例させる
    // (PlacementRescaleCommand.h。カント・緩和曲線の変更が対象)。
    bool SetSelectedEdgeProperty(Editor& editor,
                                 const rs::world::Guid& selectedEdge,
                                 const EdgePropertyDelta& delta,
                                 rs::world::ManualPlacementOverlay* overlay = nullptr);

    // edgesそれぞれにmakeDelta(edge)の差分を適用し、全体を1回のUndoにする。
    // makeDeltaがnulloptを返したedgeは飛ばす。1本も変えなければ何もせずfalse。
    // 差分をedgeごとに計算させるのは、値が今のedge次第で変わる編集(カント自動をONにする
    // ときの最大カントの初期値など)を、1つの値の使い回しにしないため。
    using EdgeDeltaFn = std::function<std::optional<EdgePropertyDelta>(const rs::world::TrackEdge&)>;
    bool SetEdgesProperty(Editor& editor,
                          const std::vector<rs::world::Guid>& edges, const EdgeDeltaFn& makeDelta,
                          rs::world::ManualPlacementOverlay* overlay = nullptr);

    // 選択edgeのtrackPluginsにfolderUtf8が含まれていれば外し、
    // 含まれていなければ追加する(Undo対応、SetSelectedEdgeProperty経由。戻り値は
    // その呼び出し結果をそのまま伝播する)。
    bool ToggleSelectedEdgeTrackPlugin(Editor& editor,
                                       const rs::world::Guid& selectedEdge,
                                       const std::string& folderUtf8);

    // edge内プラグイン区間。既に選択edgeに所属しているfolderUtf8が
    // [sStart,sEnd]の範囲でだけ有効になるよう、edgeの全区間を書き換える(Undo対応)。
    // sEnd<0はedge終端まで。他のフォルダは各区間でそのまま維持する。未所属なら
    // 何もせずfalseを返す。
    bool SetSelectedEdgeFolderRange(Editor& editor,
                                    const rs::world::Guid& selectedEdge,
                                    const std::string& folderUtf8, double sStart, double sEnd);

    // 選択edgeの緩和曲線プラン(種別/長さ)を変更する。edge.refit.available な3点フィレット
    // 由来のedgeのみ対象: 両端ノードの接線交点から頂点Cを復元し、新プランで
    // フィレットを再ソルブして curve を差し替える(Undo対応)。幾何不成立・
    // plan無効・直線edge(接線平行)は false を返しグラフは変更しない。選択edgeが
    // 複線中心線(centerlineOnly)の場合は、独立編集済み(detachedFromCenterline)
    // でないmemberを新しい中心線curveからOffsetCurveで再生成し追従させる。
    bool SetSelectedEdgeTransition(Editor& editor,
                                   rs::world::ManualPlacementOverlay& overlay,
                                   const rs::world::Guid& selectedEdge,
                                   rs::world::AlignmentSegment::TransitionShape shape,
                                   float newTransLen);

    // ---- 分岐(ポイント)開通方向切替(方式A) ----
    // nodeGuidが分岐node(links.size()>2かつpointThroatLink設定済み)であり、
    // newActiveIndexがlinks範囲内かつpointThroatLinkと異なる場合のみ、
    // pointActiveLinkをnewActiveIndexへ変更してtrueを返す(1操作=1Undo単位)。
    // それ以外(非分岐node/範囲外/throatLink自体を指定)はfalseで何も変更しない。
    bool TrySetPointActiveLink(Editor& editor,
                               const rs::world::Guid& nodeGuid, int newActiveIndex);
};

} // namespace rs::editor

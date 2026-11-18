//
// EditorPlacementOverlayOps.h - 配置物(Pier/Pole)の手動編集・架線の手動接続編集
//
// ManualPlacementOverlay(+一部graph参照)を明示引数で受け取る
// 無状態の操作クラス(EditorNodeDrag/EditorEdgePropertiesと同じ規約)。選択状態
// (selectedPlacementEdge等)はUI/描画が参照するためEditor側に残る。
//
#pragma once

#include <optional>
#include <string>

#include "../../world/scene/ManualPlacementOverlay.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

// 移動要求が実質no-opか(edgeが変わらず、s方向・左右方向とも移動量が閾値未満)。
// no-opでもrevisionを上げるとメッシュ全再構築が走り、Undo項目も積まれてしまうため、
// コマンド発行前の事前判定と適用時の判定で同じ式を使う。
// クロスエッジ移動はs・lateralが偶然ほぼ同値でも実際の移動なのでno-op扱いしない。
bool IsNoOpPlacementMove(const rs::world::Guid& oldEdge, double oldS, float oldLateralOffsetM,
                         const rs::world::Guid& newEdge, double newS, float newLateralOffsetM);

// 架線の接続点1つ。軌道上のPole個体(edge, s, Poleプラグインのfolder)と、その柱の
// 接続点添字(WireLateralOffsetsForの並び順)で指す。実体化の前後で同じ値を指せる。
// poleIdがNilでないときは、edge・s・poleFolderUtf8を使わず、そのGuidの柱(Structの接続点から
// 導出した柱)を直接指す。
struct WirePointRef {
    rs::world::Guid edge = rs::world::NilGuid();
    double s = 0.0;
    std::string poleFolderUtf8;
    int branch = 0;
    rs::world::Guid poleId = rs::world::NilGuid();
};

// 接続点がStructの接続点から導出した柱を指しているか(poleIdを持つか)。
bool IsStructLinePoint(const WirePointRef& point);

// 結ばれた接続点のペア(径間1本)。向きは問わない(SameWireSpanで比べる)。
struct WireSpanRef {
    WirePointRef a;
    WirePointRef b;
};

// 2つの接続点が同じ点か(sは実体化済み個体の照合と同じ許容差で比べる)。
bool SameWirePoint(const WirePointRef& a, const WirePointRef& b);

// 2本の径間が同じ接続点のペアか(端点の向きは問わない)。
bool SameWireSpan(const WireSpanRef& x, const WireSpanRef& y);

class EditorPlacementOverlayOps {
public:
    // 選択中の実体化済み配置物を削除する。対象が見つからない場合は警告を出して
    // no-opにする。
    void DeleteSelectedPlacement(rs::world::ManualPlacementOverlay& overlay,
                                 const rs::world::Guid& edge, double s,
                                 const std::string& folderUtf8);

    // 配置物を1本、実体化済みのedge×folderへ追加する。folderUtf8が空、またはedgeがgraphに無ければ
    // 何もせずfalseを返す。
    bool AddManualPlacement(rs::world::ManualPlacementOverlay& overlay,
                            const rs::world::TrackGraph& graph, const rs::world::Guid& edge,
                            double s, const std::string& folderUtf8, bool keepUpright,
                            float verticalOffsetM);

    // 配置物(oldEdge,oldS,folderUtf8)をnewEdge上のnewS・newLateralOffsetMへ移動する
    // (edge沿いのs方向だけでなく、lateralOffsetMで左右方向にも動かせる。
    // oldEdge!=newEdgeの隣接edgeへの移動(クロスエッジ移動)にも対応)。
    // newEdgeが存在しない、またはoldEdge==
    // newEdgeかつs・lateralOffsetMどちらも移動量が無視できるほど小さければ
    // 何もせずfalseを返す(s単独の閾値ではlateralのみのドラッグを取りこぼすため、
    // 2軸+edge一致判定の複合判定にした)。
    // newDir: 移動先(newEdge)でのTrackPosition::dir。クロスエッジ移動時は
    // WalkChainDirection(TrackChain.cpp)と同じdir formulaで呼び出し側
    // (PlacementDragInput.cpp)が算出し明示的に渡す(EvaluateAtのright方向=
    // lateralOffsetMの符号解釈をedge境界をまたいでも一貫させるため必須)。
    // 未指定(nullopt)ならdirは変更しない(in-place更新は既存値を保持、
    // 新規個体は既定+1)。
    // 実体化済み個体のクロスedge移動は、移動先edgeで同じfolderが自動生成中の場合に
    // 拒否してfalseを返す。この操作では移動先の実体化を行わない。
    bool MovePlacementTo(rs::world::ManualPlacementOverlay& overlay,
                         const rs::world::TrackGraph& graph, const rs::world::Guid& oldEdge,
                         double oldS, const std::string& folderUtf8,
                         const rs::world::Guid& newEdge, double newS, bool keepUpright,
                         float verticalOffsetM, float oldLateralOffsetM = 0.0f,
                         float newLateralOffsetM = 0.0f, std::optional<int> newDir = std::nullopt);

    // 接続点pointAとpointBを結ぶ径間を1本置く。同じ2点を結ぶ既存の径間は(架線プラグインが
    // 違っても)先に消して置き換える。柱がオーバーレイに無い・同じ点どうし・poleIdの無い
    // ワールドアンカー柱のときは何もせずfalse。poleId付きの点は柱の存在を確かめずGuidで結ぶ
    // (導出した柱はオーバーレイに無い)。
    bool ConnectWirePoints(rs::world::ManualPlacementOverlay& overlay, const WirePointRef& pointA,
                           const WirePointRef& pointB, const std::string& lineFolderUtf8);

    // 接続点pointAとpointBを結ぶ径間だけを消す(向きは問わない)。消えたらtrue。
    bool DisconnectWirePoints(rs::world::ManualPlacementOverlay& overlay,
                              const WirePointRef& pointA, const WirePointRef& pointB);

    // fromEdgeにアンカーされた実体化済み配置をtoEdgeへ付け替える(sはそのまま
    // 流用)。複線中心線が退化メンバでカスケード削除される際、消える前にそこへ
    // アンカーされていた個別編集を残存する実レールedgeへ引き継ぐために使う。
    void ReanchorPlacementEditsForEdge(rs::world::ManualPlacementOverlay& overlay,
                                       const rs::world::Guid& fromEdge,
                                       const rs::world::Guid& toEdge);
};

} // namespace rs::editor

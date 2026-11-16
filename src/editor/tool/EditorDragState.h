//
// EditorDragState.h - Editorが抱えるUndo対象外のドラッグ/ホバー一時状態(POD)
//
// Editor.h から分けた。ドラッグ中のプレビュー値
// など「Undo対象外の直接mutate」だけをここへ集約する。ロジックは持たず、
// Cancel()系の最小限のクリア処理のみを置く。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/TrackGraph.h" // rs::world::Guid/NilGuid
#include "NodeDragPlan.h" // rs::editor::EdgeShapePreview

namespace rs::editor {

// ドラッグ中のedge形状プレビュー。NodeDragPlan(NodeDragPlan.h)の ok と edges を
// そのままUI状態として保持する(ok==falseならプレビュー曲線を描かず、ガイド表示をエラー色にする)。
struct DragCurvePreview {
    bool ok = false;
    std::vector<EdgeShapePreview> edges;
    void Clear() { ok = false; edges.clear(); }
};

// press-drag-release方式(押す→ドラッグ→離す、PlacementDragUiStateと同じ操作系)の
// ノード位置ドラッグ(EditorViewport.cppが
// 処理、trackCount等と同じくUndo対象外の直接mutate)。
struct NodeDragUiState {
    bool active = false;
    rs::world::Guid guid = rs::world::NilGuid();
    rs::core::Vec3 previewPos;
    float planeY = 0.0f; // ドラッグ開始時のnode標高(この高さの水平面上を追従)
    // レール頭とnode位置の高さの差[m]。カーソルが指す水平面はレール頭の高さに合わせ、
    // 保存するnode位置はこの差を引く(表示・判定がレール頭基準のため)。
    float railLift = 0.0f;
    // ドラッグ中の高さ変更[m](↑↓キー/Ctrl+ホイール、PlacementHeightAction)。ドラッグ開始時は0で、
    // 確定・キャンセルで0へ戻す。ノードの高さ = planeY + heightOffset。
    float heightOffset = 0.0f;
    DragCurvePreview preview; // ドラッグ中のedge形状プレビュー(BuildEdgeShapeのspline)
    void Cancel() {
        active = false;
        guid = rs::world::NilGuid();
        heightOffset = 0.0f;
        railLift = 0.0f;
        preview.Clear();
    }
};

// 接線ハンドルの長さ[m](表示・ヒットテストの両方で共有)。
constexpr float kTangentHandleLengthM = 4.0f;

// 接線ハンドルのドラッグ(press-drag-release方式、NodeDragUiStateと同じ操作系。
// ノード本体の判定と競合しにくいようにこの方式にしている)。
// ドラッグはXZ方向(水平回転)のみを操作し、垂直(Y)成分はドラッグ開始時点の元の
// 接線のY成分をそのまま引き継ぐ(勾配は変更しない)。次数2(通過点)ノードは
// 前後どちらのハンドルを掴んでも同じ接線(1本)を回転させる。
struct TangentDragUiState {
    bool active = false;
    rs::world::Guid guid = rs::world::NilGuid();
    rs::core::Vec3 previewTangent; // 単位ベクトル(Y成分はoriginalTangentY由来)
    float planeY = 0.0f; // ドラッグ開始時のnode標高(この高さの水平面上でレイと交差させる)
    float originalTangentY = 0.0f; // ドラッグ開始時点の元の接線のY成分(勾配を維持するため)
    // 掴んだのが正方向(+1、node.tangentそのものの向き)か逆方向(-1、次数2ノードのみ
    // 表示される反対側のハンドル)か。逆方向ハンドルはXZ入力の符号を反転させて
    // previewTangent(常に正方向の値)を求める。
    int side = +1;
    DragCurvePreview preview; // ドラッグ中のedge形状プレビュー(BuildEdgeShapeのspline)
    void Cancel() { active = false; guid = rs::world::NilGuid(); side = +1; preview.Clear(); }
};

// 通常Selectモードで左ボタンを押した瞬間に掴んだ対象。ドラッグの開始は、押した時点の
// この記録で決める(開始時のホバーや選択は見ない。ホバーは動かすうちに外れ、選択は
// 押下で入れ替わるため)。
enum class PressTarget { None, TangentHandle, Node };

struct SelectPressUiState {
    PressTarget target = PressTarget::None;
    rs::world::Guid node = rs::world::NilGuid(); // 掴んだノード(ハンドルの場合は持ち主のノード)
    int side = +1;                               // ハンドルの向き(+1=正方向、-1=逆方向)
    void Clear() {
        target = PressTarget::None;
        node = rs::world::NilGuid();
        side = +1;
    }
};

// 空き地から左ドラッグで四角を描いてレールを選ぶ操作(矩形選択)の状態。スクリーン座標[px]。
// armed=空き地を押して待っている、active=しきい値を超えて四角を描いている。
struct BoxSelectUiState {
    bool armed = false;
    bool active = false;
    float startX = 0.0f, startY = 0.0f;
    float endX = 0.0f, endY = 0.0f;
    void Clear() { *this = BoxSelectUiState{}; }
};

// ---- 配置物のedge沿い移動
//      ＋左右自由移動(情景モードでの自由移動) ----
// ドラッグ中の状態。selectedPlacement*の値を起点として、ドラッグしきい値超過時に
// EditorViewport.cppがセットする(Undo対象外の直接mutateで統一)。
struct PlacementDragUiState {
    bool active = false;
    rs::world::Guid edge = rs::world::NilGuid();
    double originS = 0.0;
    std::string folder;
    double previewS = 0.0;
    // ドラッグ開始時のplacementワールドY(水平面の高さ)。ProjectPlacementOffsetOnEdge
    // がレイとこの高さの平面との交点からs/lateralOffsetMを求める(NodeDragUiState::planeYと
    // 同じ規約)。
    float planeY = 0.0f;
    // ドラッグ開始時のlateralOffsetM(起点)と、ドラッグ中に更新するプレビュー値。
    float originLateralOffsetM = 0.0f;
    float previewLateralOffsetM = 0.0f;
    // ドラッグ開始時、選択中placementの現在のkeepUpright/verticalOffsetMを
    // app層(EditorViewport.cpp)がplacementCacheから引き継いでおく(auto配置を
    // 移動先の個体へそのまま渡すため、AddManualPlacement
    // と同じ理由)。
    bool keepUpright = false;
    float verticalOffsetM = 0.0f;

    // ---- クロスエッジ移動(隣接edgeへの個別移動) ----
    // ドラッグ開始時のedge/dirを固定で覚えておく(識別子側=MovePlacementToの
    // oldEdge/oldSに使う。edgeはホップの度に「現在のプレビューedge」
    // へ書き換わるため、識別子には使えない)。originDirは
    // WalkChainDirection(TrackChain.cpp)のdirSignに相当する不変値で、ホップ毎の
    // dir計算(newDir = (相手nodeでの自edge接続端==nodeA?+1:-1) *
    // originDir)に使う。複数edgeを跨ぐドラッグでも、この値は
    // 「経路全体を通した一貫した進行方向」を表すため書き換えない(2エッジ先の
    // dirが直前エッジのdirから単純に導出できないことがあるため、TrackChain.cppと
    // 同じ「固定dirSign」規約に合わせる)。
    rs::world::Guid originEdge = rs::world::NilGuid();
    int originDir = +1;
    // 現在のプレビューedge上でのdir(ホップの度に更新、コミット時にnewDirとして渡す)。
    int direction = +1;
};

// ---- 区間境界ドラッグ/分割プレビュー(Undo対象外、placementDragUi等と同じUI状態) ----
struct SectionEditUiState {
    bool boundaryDragActive = false;   // 区間境界をドラッグ中か
    std::size_t boundaryDragIndex = 0; // ドラッグ中の境界の添字
    double boundaryDragPreviewS = 0.0; // ドラッグ中のプレビュー位置[m]
    // ドラッグを開始したedge。境界の添字はedgeごとの意味しか持たないので、
    // ドラッグ中に選択edgeが変わった場合は確定させず破棄する(添字だけが残ると
    // 別edgeの無関係な境界を動かしてしまう)。
    rs::world::Guid boundaryDragEdge = rs::world::NilGuid();

    // 区間境界ドラッグを確定させずに中断する。区間分割モードを抜けたとき、
    // ビューポート入力が区間分割の分岐に入らなくなったときに呼ぶ
    // (この分岐の中でしかクリアされないと、掴んだままモードを抜けた場合に
    // フラグが立ち残り、再入時の最初のマウスリリースで誤確定する)。
    void CancelBoundaryDrag() {
        boundaryDragActive = false;
        boundaryDragEdge = rs::world::NilGuid();
    }
    bool splitHoverValid = false;      // 分割プレビュー(選択edge上のマウス位置)が有効か
    double splitHoverS = 0.0;          // 分割プレビュー位置[m]
};

} // namespace rs::editor

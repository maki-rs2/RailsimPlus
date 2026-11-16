//
// TrackLayingSession.h - 円弧/複線敷設の状態機械//
// Editor が抱えていた「敷設中の可変状態」と、その状態に依存するクリック処理/
// 確定処理/プレビュー構築を1クラスへまとめたもの。editor/tool 内部に閉じており、
// Editor が private メンバ(layingSession_)として保持する。back-reference は
// 持たず、Editor に広く依存するメソッド(OnClickWorld/CommitArc等)は第1引数に
// Editor& ed を取る。
//
// Editor.h からの循環includeを避けるため、本ヘッダーは Editor の前方宣言のみを
// 持つ(実体参照は TrackLayingSession.cpp / TrackLayingSessionCommit.cpp 側で
// Editor.h をincludeして行う)。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/TrackGraph.h"
#include "EditorTypes.h" // PlacePreview

namespace rs::editor {

class Editor;

// 定義は EditorPlacementSolution.h(editor/tool内部)。SolvePlacementの宣言で
// 参照するだけなので、本ヘッダーには定義を持ち込まない。
struct PlacementSolution;
// 定義は TrackLayingCommit.h(同上)。
struct LaidTracksInput;

class TrackLayingSession {
public:
    // 敷設中間点(midpoint)クリックの、始点からの最小距離[m]。これ未満は無効
    // クリックとして無視する。
    static constexpr float kMinMidpointDistanceM = 0.5f;

    // ---- 敷設フロー（ワールド座標直接指定, ヘッドレステスト用） ----
    // スクリーン座標を経由せず、ワールド座標で「クリック」を模擬する。
    // 既存nodeへのスナップ判定はワールド距離(snapRadius)で行う。
    void OnClickWorld(Editor& ed, const rs::core::Vec3& worldPos, float snapRadius);

    // 右クリック/Esc相当。1段階戻す(終点待ち->中間点待ち->始点解除)。
    void CancelPlacement(Editor& ed);

    // 現在のマウス位置から敷設プレビューを組み立てる（毎フレーム呼ぶ）。
    PlacePreview BuildPreview(const Editor& ed, float screenX, float screenY,
                              const rs::core::Mat4& viewProj, uint32_t width,
                              uint32_t height) const;

    // BuildPreview のワールド座標版。スクリーン→ワールド変換とノードスナップ判定を済ませた
    // 入力(hitPos=地面ヒット or スナップnode座標、snapNode=スナップnode)から作る。
    // ヘッドレステストの入口でもある。
    PlacePreview BuildPreviewAt(const Editor& ed, const rs::core::Vec3& hitPos,
                                const rs::world::Guid& snapNode) const;

    // BuildPreviewAt をワールド座標の「マウス位置」から呼ぶ(OnClickWorld と同じ
    // ノードスナップ判定。ヘッドレステスト用)。
    PlacePreview BuildPreviewWorld(const Editor& ed, const rs::core::Vec3& worldPos,
                                   float snapRadius) const;

    // 円弧ツール用クリック処理(RailSim2流3点指定)。hitPos=地面ヒット or
    // スナップnode座標。OnClickScreen(Editorの責務、スクリーン→ワールド変換後)
    // からも直接呼ばれるため公開する。
    void HandleClickWorldImplArc(Editor& ed, const rs::core::Vec3& hitPos,
                                 const rs::world::Guid& snappedNode);

    // ---- 既存ノード複数選択による複線開始 ----
    // 未placing状態でノードにスナップしたクリックのたび選択列へトグル追加/解除
    // される([0]=基準線)。app/uiのオーバーレイがハイライトに使う。
    const std::vector<rs::world::Guid>& SelectedStartNodes() const { return selectedStartNodes_; }

    // 最後のノード開始試行(地面クリックでの検証)が拒否されたか。UIパネル表示用。
    bool StartSelectionRejected() const { return startSelectionRejected_; }
    const char* StartSelectionRejectReason() const { return startSelectionRejectReason_; }
    // 敷設の外の操作(Alt+クリックのedge分割等)が拒否されたとき、同じ表示経路で理由を出す。
    // reasonは文字列リテラルなど、呼び出し後も有効な文字列を渡す。
    void NoteOperationRejected(const char* reason) {
        startSelectionRejected_ = true;
        startSelectionRejectReason_ = reason;
    }
    void ClearOperationRejected() { startSelectionRejected_ = false; }

    // 直近の確定が拒否された理由(どの線がなぜ敷けないか)。拒否されていなければ空。
    // 複線は1本でも作れなければ全体を拒否する。
    const std::string& CommitRejectReason() const { return commitRejectReason_; }

    // ---- 複線×S字接続: 終点ノードの明示複数選択 ----
    const std::vector<rs::world::Guid>& SelectedEndNodes() const { return selectedEndNodes_; }
    int ActiveTrackCount() const { return activeTrackCount_; }
    bool ExistingNodeStart() const { return existingNodeStart_; }

    // 現在敷設操作の途中か(始点確定済み〜終点待ち)。スペース+edgeクリック開始判定
    // (「idle状態でのみ発動」)に使う。
    bool IsPlacing() const { return placing_; }

    // edgeクリックでの分岐敷設開始。edge上の弧長位置sを「未確定の
    // 仮想開始点」として敷設を開始する(元edgeはこの時点では分割しない)。
    // CheckEdgeSplit(graph, edge, s)がNone以外を返す場合は開始せずfalseを返す
    // (startSelectionRejected_/startSelectionRejectReason_へ理由を設定)。
    // placing_中(既に敷設操作中)の場合もfalseを返す(このジェスチャは開始専用)。
    bool StartBranchFromEdge(Editor& ed, const rs::world::Guid& edge, double s);
    // 分岐の始点にできるedgeか。複線中心線自身と、いずれかの中心線のメンバー実レールは対象外。
    static bool IsBranchableEdge(const rs::world::TrackGraph& graph, const rs::world::Guid& edge);

private:
    friend class Editor;
    void PruneInvalidReferences(const rs::world::TrackGraph& graph);
    // 敷設中の状態。
    bool placing_ = false;
    rs::core::Vec3 placeStartPos_;
    rs::world::Guid placeStartNode_ = rs::world::NilGuid(); // Nilなら新規node

    // 円弧ツール(3点指定)専用の状態。
    // 始点接線が既知（既存nodeのdir、または直前確定edgeの終端接線を連続敷設で
    // 引き継いだ場合）なら haveStartTangent_=true とし、中間点は始点接線の
    // レイ上へ射影される。
    bool haveStartTangent_ = false;
    rs::core::Vec3 startTangent_; // 単位・水平

    // 中間点確定済みか（true なら次クリックは終点=確定クリック）。
    bool haveMidpoint_ = false;
    rs::core::Vec3 midPos_; // 確定済み中間点

    // 平行線(track1..N-1)の開始node。空/Nilなら新規。連続敷設で複線の端点を
    // 引き継ぐために使う。CancelPlacementでクリアする。
    std::vector<rs::world::Guid> parallelStartNodes_;

    // 複線中心線(centerlineOnly)の開始node。Nilなら新規(=前回複線敷設が無かった、
    // またはCancelPlacementでクリアされた)。連続敷設(cancelせず続けて複線を敷く)で
    // 前回の中心線終端nodeをそのまま次回の始点nodeとして再利用することで、隣接する
    // 中心線edge同士が実際に1つのjunction nodeを共有するようにする(連続敷設
    // ジャンクションでも中心線をドラッグできるようにするため)。前回commitでN<2(単線)だった、
    // またはcenterOc.invalidで中心線が作られなかった場合はNilへ戻す(鎖が途切れる)。
    rs::world::Guid centerlineStartNode_ = rs::world::NilGuid();

    // 既存ノード複線開始の選択列([0]=基準線)。未placing状態でのノードクリックで
    // トグル追加/解除される。地面クリックで消費されクリアされる。
    std::vector<rs::world::Guid> selectedStartNodes_;

    // 複線×S字接続: 終点ノードの明示複数選択列。existingNodeStart_&&activeTrackCount_>1
    // の終点待ちで、ノードスナップクリックのたびトグル追加/解除される。
    std::vector<rs::world::Guid> selectedEndNodes_;

    // 敷設開始が既存ノード由来か(true)/新規点由来か(false)。
    // CancelPlacement・CommitMultiTrackNodes確定でリセットする。
    bool existingNodeStart_ = false;

    // 敷設中の実効本数(新規点/既存ノードで統一)。CommitArcはこれを唯一の
    // 情報源として本数を決める。
    int activeTrackCount_ = 1;

    // 全track(track0..N-1)の横オフセット[m](size = activeTrackCount_)。index i は
    // 「直接ソルブされた基準curve」からのオフセットで、track0自身も特別扱いせず
    // この配列の値でOffsetCurveされる。新規点開始は全本数の中心が0になるよう
    // 等間隔配置(奇数本なら中央trackが厳密に0)、既存ノード開始はindex0=0.0f固定+
    // 既存ノード間の実際の距離。CommitArcが参照する。
    std::vector<float> parallelOffsets_;

    // スペース+edgeクリックでの分岐敷設開始。Nilでなければ、敷設決定時に
    // このedgeをpendingSplitS_で分割してから新edgeを接続する(元edgeはこの時点では
    // まだ未変更、CancelPlacementでクリアするだけで元に戻る)。
    rs::world::Guid pendingSplitEdge_ = rs::world::NilGuid();
    double pendingSplitS_ = 0.0;

    // 最後のノード開始試行(地面クリックでの検証)が拒否されたか。UIパネル表示用。
    bool startSelectionRejected_ = false;
    const char* startSelectionRejectReason_ = nullptr;

    // 直近の確定(CommitArc/CommitMultiTrackNodes)の拒否理由。成功時と確定の開始時に空へ戻す。
    // プレビューが敷設できる状態になったときも空へ戻すため、constのBuildPreviewAtから触れる。
    mutable std::string commitRejectReason_;

    // 中間点決定処理(HandleClickWorldImplArcから、および既存ノード複線開始
    // フローの「地面クリックをそのまま中間点として処理」からも呼ぶ小ヘルパ)。
    void HandleMidpointClick(const rs::core::Vec3& pos);

    // 円弧確定処理（HandleClickWorldImplArc から、終点クリックのときに呼ぶ）。
    void CommitArc(Editor& ed, const rs::core::Vec3& p1, const rs::world::Guid& snappedNode);

    // 複線×S字接続: 終点ノード列(selectedEndNodes_)が activeTrackCount_ 個
    // 揃ったときに呼ばれる一括確定処理。
    void CommitMultiTrackNodes(Editor& ed);

    // 敷設ソルバの解から、複線の実レール群と中心線の姿勢の計画の入力を作る。確定と
    // プレビューが同じ入力を使う。splitNodeGuid は分岐敷設で確定時に作る分割ノードの
    // guid(プレビューは仮のguidでよい)。
    LaidTracksInput BuildLaidTracksInput(const Editor& ed, const PlacementSolution& sln,
                                         const rs::core::Vec3& startTangentUsed,
                                         const rs::world::Guid& snappedNode,
                                         const rs::world::Guid& splitNodeGuid) const;

    // 始点接線が既知のとき、クリック点(rawPos)を始点からの前方レイ
    // P0+s*t0(s>0) 上へ射影した中間点候補を返す。s<0.05mなら無効(false)。
    bool ProjectMidpointOnTangentRay(const rs::core::Vec3& rawPos, rs::core::Vec3& outMid) const;

    // 現在の敷設状態(haveStartTangent_/midPos_/placeStartPos_)から始点接線を解決する。
    // 求まらなければ false。
    bool ResolveStartTangent(rs::core::Vec3& out) const;

    // 敷設ソルバ本体。始点フレーム(startPos/startTangent)・中間点(midPos)・
    // 終点候補(hitPos)・終点スナップnodeから、確定用curveと表示情報を解く。
    // BuildPreview と CommitArc の唯一の共通ソルバ(二重実装を排除)。
    bool SolvePlacement(const Editor& ed, const rs::core::Vec3& startPos,
                       const rs::core::Vec3& startTangent, const rs::core::Vec3& midPos,
                       const rs::core::Vec3& hitPos, const rs::world::Guid& snappedNode,
                       PlacementSolution& out) const;
};

} // namespace rs::editor

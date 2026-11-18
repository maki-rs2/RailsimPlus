//
// Editor.h - 線路敷設エディタ本体
//
// マウス/スクリーン座標のピッキング・敷設フローの状態機械を持つ。GUI描画その
// ものは app/ui 側（ViewportOverlay/UiPanels）が担当し、本クラスは「スクリーン
// 座標→ワールド操作」の変換と TrackGraph への編集(コマンド経由)のみを受け持つ。
//
// 個々の編集操作の約束事(事前条件・v1スコープ・戻り値の意味)は、委譲先である
// EditorNodeDrag / EditorEdgeProperties / EditorPlacementOverlayOps の各ヘッダー
// に書いてある。本ヘッダーのフォワーダ側には重複して書かない(二重管理で片方が
// 古くなるため)。Editor側の責務はrevision更新と選択状態のガードだけ。
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SurfaceBounds.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "../../world/scene/MaterializeSnapshot.h"
#include "PlacementEditCommand.h"
#include "SpeedSignalOwnerCandidate.h"
#include "PlacementOverlayCommand.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "Command.h"
#include "SceneEntityCommand.h"
#include "EdgeSelection.h"
#include "EditorDragState.h"
#include "EditorEdgeProperties.h"
#include "EditorNodeDrag.h"
#include "EditorPlacementOverlayOps.h"
#include "PlatformCommand.h"
#include "BlockCommand.h"
#include "SpeedSignalCommand.h"
#include "EditorSectionEdit.h"
#include "EditorTypes.h" // ToolMode / PlacePreview / ProjectedPoint / Ray / SectionLayer
#include "PlacementSnap.h"
#include "TrackLayingCommit.h"
#include "TrackLayingSession.h"
#include "TrackLayingSettings.h"

namespace rs::legacy_pi {
struct PluginDefinition;
} // namespace rs::legacy_pi

namespace rs::world { class WarpLinkTable; }  // MergeEdgeAtのポインタ引数用の前方宣言

namespace rs::world { class SceneRegistry; }

namespace rs::editor {

namespace detail {
}

// 配置位置と担当候補の検証結果。仮止め中もマーカーと候補表示が同じ結果を読む。
struct SpeedSignalHover {
    bool hasTarget = false;
    rs::world::Guid edge = rs::world::NilGuid();
    double s = 0.0;
    // 候補が1つのときの担当閉塞。それ以外と表示専用の信号はNil。
    rs::world::Guid block = rs::world::NilGuid();
    rs::world::SpeedSignalPlacementError error = rs::world::SpeedSignalPlacementError::None;
    // edgeのs軸に対して+1=順、-1=逆。仮止め中は向きを固定する。
    int direction = +1;
    std::vector<SpeedSignalOwnerCandidate> candidates;
};

// 最後に置いた信号または駅舎アンカーの、edgeとs軸に対する向き。次に置くものの向きの基準になる。
// 駅の番線1本ぶんのレール生成計画。definitionIndex は Platform{} の定義順の添字。
struct PlatformRailPlan {
    size_t definitionIndex = 0;
    int parentPartIndex = -1;  // 親の部品に追従する番線のときだけ0以上
    LaidEdgePlan plan;
    rs::world::EdgeShapeBuild shape;
};

struct PlacedTravel {
    rs::world::Guid edge = rs::world::NilGuid();
    int direction = +1;
};

class Editor {
public:
    struct SceneEntityGizmoDragState {
        bool active = false;
        bool rotation = false;
        // 中央の円のドラッグ。視点に垂直な平面の上で、3軸を同時に動かす。
        bool free = false;
        int axisIndex = 0;
        rs::core::Vec3 pivot;
        rs::core::Vec3 axis;
        rs::core::Vec3 planeNormal;
        rs::core::Vec3 startPlanePoint;
        // 回転の向き。+1なら、画面上で反時計回りにドラッグすると正の回転になる。
        double rotationSign = 1.0;
        // 画面に出す、いま動かしている量。軸の移動はm、回転はラジアン、中央のドラッグは3軸のオフセット[m]。
        double currentDelta = 0.0;
        rs::core::Vec3 currentFreeOffset;
        rs::core::Vec2 screenCenter;
        rs::core::Vec2 screenAxis;
        rs::core::Vec2 startMouse;
        float worldPerPixel = 0.0f;
        double startScreenAngle = 0.0;
        std::vector<rs::world::SceneEntity> before;
    };
    // カーソルが当たっているギズモの部品。描画のホバー表示に使う。
    struct SceneEntityGizmoHoverState {
        bool valid = false;
        bool center = false;
        bool rotation = false;
        int axisIndex = 0;
    };
    // ピッキング半径の既定値[px]（PickNode/PickEdge/PickEdgeSの既定引数）。
    static constexpr float kDefaultNodePickRadiusPx = 8.0f;
    static constexpr float kDefaultEdgePickRadiusPx = 8.0f;
    // 敷設のedge上スナップ・Alt+クリック分割とそのマーカーの半径[px]。選択より広めにして、
    // edgeの近くまで寄せれば吸着するようにする。
    static constexpr float kEdgeSnapRadiusPx = 20.0f;

    // ---- 状態 ----
    rs::world::TrackGraph graph;
    // 配置物(架線柱/橋脚/架線)の手動編集を自動配置へ重ねる永続差分。
    // EditorViewport が
    // CollectTrackPluginPlacements の結果へ後段適用する(空なら no-op)。
    rs::world::ManualPlacementOverlay manualOverlay;
    CommandStack stack;

    void BindSceneRegistry(rs::world::SceneRegistry& sceneRegistry) { sceneRegistry_ = &sceneRegistry; }

    ToolMode mode = ToolMode::Select;

    // ---- 敷設設定（app/uiのパネルから変更される。詳細はTrackLayingSettings.h参照） ----
    TrackLayingSettings layingSettings;
    // 現在のSceneのSurfaceの範囲。あれば、新規ノードの置き場所(ResolvePlacementSnap)を範囲内へ押し戻す。
    // appが毎フレーム更新する(Surfaceが無ければnullopt)。
    std::optional<rs::world::SurfaceBounds> surfaceBounds;
    // posをSurfaceの範囲内へ押し戻す(範囲が無ければ何もしない)。Yは変えない。範囲内だった(または範囲が無い)ときだけtrue。
    bool ClipToSurface(rs::core::Vec3& pos) const {
        return !surfaceBounds || rs::world::ClipToSurfaceBounds(*surfaceBounds, pos);
    }

    // ホバー/選択状態（app/ui のオーバーレイ描画・プロパティパネルが参照）
    rs::world::Guid hoveredNode = rs::world::NilGuid();
    // 選択中ノードの接線ハンドル先端にマウスが乗っているか(対象は常にselectedNodeの
    // ため識別子は不要)。次数2(通過点)ノードは前後どちらのハンドルを
    // ホバー中かをhoveredTangentHandleSideに保持する(+1=正方向/-1=逆方向)。
    // hoveredTangentHandleは「いずれかのハンドルをホバー中か」の
    // 意味で残す(hoveredTangentHandleSide!=0と同値)。
    bool hoveredTangentHandle = false;
    int hoveredTangentHandleSide = 0;
    rs::world::Guid hoveredEdge = rs::world::NilGuid();
    rs::world::Guid selectedEdge = rs::world::NilGuid();
    rs::world::Guid selectedNode = rs::world::NilGuid();
    rs::world::Guid selectedStation = rs::world::NilGuid();
    rs::world::Guid selectedPlatform = rs::world::NilGuid();
    rs::world::Guid selectedBlock = rs::world::NilGuid();
    SignalBlockArmedAction signalBlockArmedAction = SignalBlockArmedAction::None;
    // 信号を置いたあとも配置待ちを続けるか(情景の連続配置と同じ設定。RmlUiのブラウザが毎フレーム書く)。
    bool signalPlacementContinuous = false;
    // 信号の配置待ち中のホバー結果(向きも含む)。
    SpeedSignalHover speedSignalHover;
    std::optional<SpeedSignalHover> speedSignalPending;
    bool CancelSpeedSignalPending() {
        if (!speedSignalPending) return false;
        speedSignalPending.reset();
        speedSignalHover = {};
        return true;
    }
    // 最後に置いた信号または駅舎アンカー(信号と駅舎で共通)。参照先のedgeが無くなったら基準なしとして扱う。
    std::optional<PlacedTravel> lastPlacedTravel;
    // 駅・ダイヤパネルでマウスが乗っている行の対象。ビューポート側で対象edgeを
    // ハイライトするためだけのUI状態で、Undo対象ではない(hoveredNode/hoveredEdgeと同じ扱い)。
    // パネルは毎フレーム先頭でNilGuidへ戻してから、ホバー中の行で上書きする。
    rs::world::Guid hoveredStation = rs::world::NilGuid();
    rs::world::Guid hoveredPlatform = rs::world::NilGuid();
    // 同じく、ポイント一覧でマウスが乗っている行のnode。
    rs::world::Guid hoveredPointNode = rs::world::NilGuid();
    // 同じく、分岐の条件の閉塞の一覧でマウスが乗っている行の閉塞(3D画面でそのレールを強調する)。
    rs::world::Guid hoveredBlock = rs::world::NilGuid();
    // selectedPlacement/selectedSceneEntityと対称なクリアAPI(UI側がselectedNode/
    // selectedEdgeへ生代入するカプセル化違反を防ぐ)。
    // edge選択の変更は必ずここを通す(区間の選択添字は前のedgeの区間を指している
    // ため、edgeが変わったらリセットする必要がある。ResetSelectedSectionIndices)。
    //
    // 選択中のレールは複数持てる(edgeSelection_)。selectedEdgeはその「主」(最後に選んだ1本、
    // 無ければNil)の読み取り用の写しで、区間編集・緩和曲線のように1本を前提にする処理が読む。
    // 直接代入せず、必ず下のAPIを通す(集合との食い違いを防ぐ)。
    void SelectEdge(const rs::world::Guid& edge) {
        edgeSelection_.SetSingle(edge);
        SyncSelectedEdge();
    }
    void ClearSelectedEdge() { SelectEdge(rs::world::NilGuid()); }
    // 選ばれていれば外し、無ければ加える(Shift+クリック、矩形選択の追加)。
    void ToggleEdgeSelection(const rs::world::Guid& edge) {
        edgeSelection_.Toggle(edge);
        SyncSelectedEdge();
    }
    // edgesだけを選ぶ(順番どおり、最後が主)。矩形選択の確定用。
    void SelectEdges(const std::vector<rs::world::Guid>& edges) {
        edgeSelection_.Clear();
        for (const rs::world::Guid& edge : edges) edgeSelection_.Add(edge);
        SyncSelectedEdge();
    }
    void AddEdgeToSelection(const rs::world::Guid& edge) {
        edgeSelection_.Add(edge);
        SyncSelectedEdge();
    }
    // 選んだ順のレール。
    const std::vector<rs::world::Guid>& SelectedEdges() const { return edgeSelection_.Edges(); }
    bool IsEdgeSelected(const rs::world::Guid& edge) const { return edgeSelection_.Contains(edge); }
    std::size_t SelectedEdgeCount() const { return edgeSelection_.Count(); }
    // 結合などでedgeが別のedgeに置き換わったとき、選択も追従させる。
    void ReplaceSelectedEdge(const rs::world::Guid& removed, const rs::world::Guid& kept) {
        edgeSelection_.Replace(removed, kept);
        SyncSelectedEdge();
    }
    // graphから消えたレールを選択から外す(Undo/Redoの後など)。
    void PruneSelectedEdges() {
        if (edgeSelection_.RetainExisting(graph)) SyncSelectedEdge();
    }
    void ClearSelectedNode() { selectedNode = rs::world::NilGuid(); }
    void ClearSelectedStation() { selectedStation = rs::world::NilGuid(); }
    void ClearSelectedPlatform() { selectedPlatform = rs::world::NilGuid(); }
    void ClearSelectedBlock() { selectedBlock = rs::world::NilGuid(); }

    bool ExecutePlatformCommand(std::unique_ptr<ICommand> command) {
        return ExecuteCommand(std::move(command));
    }

    // edge上のsにdirectionの向きで置く駅舎アンカーの配置(プレビューと確定で共通)。edgeが無い・形が無いときはnullopt。
    std::optional<rs::world::FreePlacement> StationAnchorPlacement(const rs::world::Guid& edge,
                                                                   double s, int direction) const;
    // edge上のsに駅舎アンカーを置く。directionは進む向き(+1=s軸、-1=逆)で、駅舎の向きに使う。
    bool AddStationAnchor(rs::world::SceneRegistry& sceneRegistry, const rs::world::Guid& edge,
                          double s, int direction, const std::string& folderUtf8);
    // edgeに信号・駅舎アンカーを置くときの向き(s軸に対して+1か-1)。基準は最後に置いたもの
    // (lastPlacedTravel)から、つながり(warpLinksを渡すと同じシーン内のワープの先も)をたどって決める。
    // 基準が無い・たどれないときはs軸の向き。reverseがtrue(Shiftを押している間)なら逆にする。
    int ResolvePlaceTravelDirection(const rs::world::Guid& edge, bool reverse,
                                    const rs::world::WarpLinkTable* warpLinks = nullptr,
                                    std::size_t sceneIndex = 0) const;
    // 保存済みの所属から駅舎の全番線を削除するコマンドを作る。対応が無ければnullptr。
    std::unique_ptr<ICommand> RemoveStationAnchorPlatform(const rs::world::Guid& sceneEntityId);
    // 駅の Platform{} ごとに、作れるレールの計画(線形と形)を返す。コマンドは作らず、グラフも変えない。
    // 配置プレビューと GeneratePlatformRails が同じ計算を使う。作れない番線は含めない。
    std::vector<PlatformRailPlan> PlanPlatformRails(const rs::world::SceneEntity& entity,
                                                    const rs::legacy_pi::PluginDefinition& definition) const;
    void GeneratePlatformRails(const rs::world::SceneEntity& entity,
                               const rs::legacy_pi::PluginDefinition& definition);

    // ホバー/選択中の自動配置物(Pier/Pole)。
    // (edge, s, folderUtf8) が identity。配置一覧(TrackPluginPlacementItem)自体はプラグイン選択状態に依存する
    // app層のデータのため、キャッシュ保持とピッキングはEditor::Pick*には置かず
    // app/UI側(EditorViewport.cpp)が担う。Editorはidentityの結果だけを
    // 選択状態として持つ。
    rs::world::Guid hoveredPlacementEdge = rs::world::NilGuid();
    double hoveredPlacementS = 0.0;
    std::string hoveredPlacementFolder;
    rs::world::Guid selectedPlacementEdge = rs::world::NilGuid();
    double selectedPlacementS = 0.0;
    std::string selectedPlacementFolder;
    bool HasSelectedPlacement() const { return selectedPlacementEdge != rs::world::NilGuid(); }
    // 選択とホバーと進行中のドラッグをすべて解除する(表示モード・編集サブモードの切替、レイアウト読込・新規)。
    void ClearSelectionAndHover();
    void ClearSelectedPlacement() {
        selectedPlacementEdge = rs::world::NilGuid();
        selectedPlacementS = 0.0;
        selectedPlacementFolder.clear();
    }

    // ホバー/選択中のScenery(SceneEntity、駅舎/施設等)。SceneRegistryが発番するguidで識別する。
    rs::world::Guid hoveredSceneEntityId = rs::world::NilGuid();
    rs::world::Guid selectedSceneEntityId = rs::world::NilGuid();
    SceneEntityGizmoDragState sceneEntityGizmoDrag;
    SceneEntityGizmoHoverState sceneEntityGizmoHover;
    bool HasSelectedSceneEntity() const { return !selectedSceneEntityIds_.Empty(); }
    bool IsSceneEntitySelected(const rs::world::Guid& id) const { return selectedSceneEntityIds_.Contains(id); }
    const std::vector<rs::world::Guid>& SelectedSceneEntities() const { return selectedSceneEntityIds_.Items(); }
    void SelectSceneEntity(const rs::world::Guid& id) {
        selectedSceneEntityIds_.SetSingle(id);
        SyncSelectedSceneEntity();
    }
    void ToggleSceneEntitySelection(const rs::world::Guid& id) {
        selectedSceneEntityIds_.Toggle(id);
        SyncSelectedSceneEntity();
    }
    void SelectSceneEntities(const std::vector<rs::world::Guid>& ids) {
        selectedSceneEntityIds_.SetItems(ids);
        SyncSelectedSceneEntity();
    }
    void ClearSelectedSceneEntity() {
        selectedSceneEntityIds_.Clear();
        SyncSelectedSceneEntity();
    }
    void DeleteSelectedSceneEntities(rs::world::SceneRegistry& sceneRegistry);
    void CommitSceneEntityPlacements(rs::world::SceneRegistry& sceneRegistry,
                                     const std::vector<std::pair<rs::world::Guid, rs::world::FreePlacement>>& updates);
    void CommitSceneEntityTransform(rs::world::SceneRegistry& sceneRegistry,
                                    const std::vector<rs::world::SceneEntity>& before);
    // ティントハイライト(FrameRenderer::highlightPipelines_[0]=ホバー,[1]=選択)の
    // 直近アップロード済み識別子(folderUtf8+switchOptions結合)。UploadHighlightMesh
    // はWaitForGpuを伴う重い同期アップロードのため、識別子が変わった時だけ呼ぶ判定に
    // 使う(Undo対象外のUI/レンダリング内部状態、placementDragUi等と同じ扱い)。
    std::string highlightUploadedKey[2];

    // グラフが変更されるたびに増分するカウンタ。呼び出し側(app/ui)はこれの
    // 変化を検知してメッシュ再構築の要否を判定する。
    uint64_t revision = 0;
    // ユーザーが置いた・消した操作の回数(効果音など、操作の通知に使う)。コマンドが実際に積まれたときだけ
    // 増やし、Undo/Redoでは増やさない。呼び出し側は前回との差で新しい操作を知る。
    std::size_t PlacedCount() const { return placedCount_; }
    std::size_t RemovedCount() const { return removedCount_; }
    void NotePlaced() { ++placedCount_; }
    void NoteRemoved() { ++removedCount_; }
    // 保存する敷設設定(layingSettings)が変わるたびに増えるカウンタ。メッシュは作り直さない。
    uint64_t layingSettingsRevision = 0;
    // レイアウトの未保存判定に使う世代(グラフの編集と敷設設定の変更の両方で進む)。
    uint64_t DocumentRevision() const { return revision + layingSettingsRevision; }
    // 保存する敷設設定を1項目書く。値が変わったときだけ layingSettingsRevision を進める。
    template <class T, class U>
    void SetLayingSetting(T TrackLayingSettings::*field, U&& value) {
        if (layingSettings.*field == value) return;
        layingSettings.*field = std::forward<U>(value);
        ++layingSettingsRevision;
    }

    // ---- スクリーン投影・ピッキング ----
    // ProjectToScreen/ScreenToRay/RayHitPlaneY/RayHitGroundPlaneは
    // 汎用カメラ幾何なので rs::core::Math.h にある。

    // nodeの接続edge群のrailTopLift最大値ぶんnode.up方向へ持ち上げた位置
    // (レール頭頂面＝Rail/Tie積み上げ後の実際の軌道高さ)。描画(EditorViewport)と
    // ピッキング(PickNode/PickEdge)は共にこの位置を基準にすることで、見た目と
    // クリック判定のズレを防ぐ。
    rs::core::Vec3 NodeRailTopPos(const rs::world::TrackNode& node) const;

    // 全nodeをスクリーン投影し、(screenX,screenY)から半径pxRadius以内で
    // 最近傍のnodeのguidを返す（無ければNilGuid）。
    rs::world::Guid PickNode(const rs::core::Ray& ray, float screenX, float screenY, const rs::core::Mat4& viewProj,
                             uint32_t width, uint32_t height,
                             float pxRadius = kDefaultNodePickRadiusPx) const;

    // 各edgeのspline点列を投影した折れ線とマウスの距離が pxRadius 以内なら
    // そのedgeのguidを返す（無ければNilGuid）。
    // skipCenterlineOnly=trueのとき複線中心線edgeを除外する(実レールと重なるため、実レールだけを選びたい用途で渡す)。
    rs::world::Guid PickEdge(const rs::core::Ray& ray, float screenX, float screenY, const rs::core::Mat4& viewProj,
                             uint32_t width, uint32_t height,
                             float pxRadius = kDefaultEdgePickRadiusPx,
                             bool skipCenterlineOnly = false) const;

    // スクリーン上の矩形(対角の2点)にかかる実体のレール(複線中心線を除く)のguid。矩形選択用。
    std::vector<rs::world::Guid> PickEdgesInRect(float x0, float y0, float x1, float y1,
                                                 const rs::core::Mat4& viewProj, uint32_t width,
                                                 uint32_t height) const;

    // PickEdgeと同じ最近傍判定に加え、ヒットしたスクリーン位置に対応するedge上の
    // 弧長s(区間内を線分距離のtで補間)も返す(手動追加で使う)。
    // skipCenterlineOnly=trueのとき複線中心線(centerlineOnly)edgeを除外する。
    // 複線中心線は実体の走行面を持たないため、列車配置のように実レールだけを
    // 対象にしたい用途ではtrueを渡す。
    rs::world::Guid PickEdgeS(const rs::core::Ray& ray, float screenX, float screenY, const rs::core::Mat4& viewProj,
                              uint32_t width, uint32_t height, double& outS,
                              float pxRadius = kDefaultEdgePickRadiusPx,
                              bool skipCenterlineOnly = false) const;

    // 指定edge1本の spline へスクリーン点を射影し、最近傍のsを返す(半径制限なし、
    // ドラッグ中プレビュー用: 既に掴んだ配置物のedgeへ追従させ続ける)。
    // edgeが存在しない/spline不足/全点が画面裏なら false。
    bool ProjectSOnEdge(const rs::world::Guid& edge, const rs::core::Ray& ray, float screenX, float screenY,
                       const rs::core::Mat4& viewProj, uint32_t width, uint32_t height,
                       double& outS) const;

    // 配置物(Pier/Pole)の自由移動ドラッグ用(情景モードでのedge沿いs＋
    // 左右lateralOffsetMの2軸ドラッグ)。マウスレイと水平面(Y=planeY、ドラッグ開始時
    // のplacementワールドY)の交点を求め、そのワールド点をedge.spline上の最近傍(3D
    // 距離)へ射影してsを、EvaluateAt(edge,s,+1)のframeから
    // right=cross(frame.up,frame.dir)方向への符号付き残差としてlateralOffsetMを返す
    // (Rs2Import.cpp::AppendImportedPluginLinksおよびResolveWorldMatrixと同じ規約)。
    // verticalOffsetMはResolveWorldMatrixと同じ高さ基準(frame.up方向の追加オフセット、
    // PoleならtrolleyAlt+height)を揃えるために必須(railTopLiftとは別物、
    // これが食い違うとplaneYと探索用polylineの高さがずれる)。edgeが存在しない/
    // spline不足/レイが平面と交差しない場合はfalse(呼び出し側は直前の値を保持する)。
    bool ProjectPlacementOffsetOnEdge(const rs::world::Guid& edge, float screenX, float screenY,
                                      const rs::core::Mat4& viewProj, uint32_t width,
                                      uint32_t height, float planeY, float verticalOffsetM,
                                      double& outS, float& outLateralOffsetM) const;

    // ProjectPlacementOffsetOnEdgeの本体(ワールド点hitをedge.spline上へ射影する
    // 部分)。隣接edgeへのクロスエッジ移動で使う。
    // レイ-平面交点を1回だけ計算し、複数の候補edge(現在のedge＋隣接edge)へ
    // それぞれこの関数を呼んで最短距離のものを選ぶ用途(PlacementDragInput.cpp)。
    // 戻り値のdistは候補edge同士の比較に使う3D距離(小さいほど近い)。
    struct PlacementEdgeProjection {
        bool found = false;
        double s = 0.0;
        float lateralOffsetM = 0.0f;
        float dist = 0.0f;
    };
    // dir: この候補edgeでその配置が実際に使うTrackPosition::dir(既定+1)。
    // 最近傍点探索(bestS)自体はdir非依存(EvaluateAtのframe.posはdirで変わらない)
    // だが、lateralOffsetMの符号基準right=cross(frame.up,frame.dir)はdirで反転する
    // (ResolveWorldMatrixが実際の描画で使うdirと必ず揃えないと、クロスエッジ移動で
    // dir=-1になる候補ではlateralOffsetMの左右が逆転して見える)。
    PlacementEdgeProjection ProjectPlacementOffsetOnEdgeAtPoint(const rs::world::Guid& edge,
                                                                 const rs::core::Vec3& hit,
                                                                 float verticalOffsetM,
                                                                 int dir = +1) const;

    // ホバー状態を更新する（node > edge > 何もなし の優先度）。
    void UpdateHover(const rs::core::Ray& ray, float screenX, float screenY, const rs::core::Mat4& viewProj, uint32_t width,
                     uint32_t height);

    // ---- 敷設フロー（スクリーン座標入力） ----
    // 左クリック相当。ツールが PlaceArc のときのみ意味を持つ。
    // RailSim2流3点指定: 1クリック目=始点、2クリック目=
    // 中間点(頂点)、3クリック目=終点(確定、SolveThreePointArcが成立すれば
    // AddEdgeCommandで確定。連続敷設は終点/終端接線から継続)。
    void OnClickScreen(float screenX, float screenY, const rs::core::Mat4& viewProj, uint32_t width,
                       uint32_t height);
    // 敷設の視線ヒット: 既存ノードにスナップできればそのノードの位置(snapNode にそのguid)、
    // できなければ敷設面(layingSettings.RayHitPlacementPoint)との交点(snapNode は Nil)。
    // クリック(OnClickScreen)とプレビュー(BuildPreview)が同じ判定を使う。
    // ノードが見つからない・視線が敷設面に当たらないときは false。
    bool ResolvePlacementHit(const rs::core::Ray& ray, float screenX, float screenY,
                             const rs::core::Mat4& viewProj, uint32_t width, uint32_t height,
                             rs::core::Vec3& hitPos, rs::world::Guid& snapNode) const;
    // 敷設のスナップを決める唯一の入口(優先順位は PlacementSnap.h の ChooseSnap)。
    // 既存ノード > 並走位置(layingSettings.trackSpacing) > カーブ開始点 > スプライン上。
    // edge上の判定(カーブ開始点・スプライン上)に当たっているときは、並走位置を候補にしない。
    // カーブ開始点とスプライン上は、edge上に始点を置く(分岐を始める)ときだけ
    // allowEdgeSnap=true で求める。ResolvePlacementHit は allowEdgeSnap=false の別名。
    bool ResolvePlacementSnap(const rs::core::Ray& ray, float screenX, float screenY,
                              const rs::core::Mat4& viewProj, uint32_t width, uint32_t height,
                              bool allowEdgeSnap, PlacementSnap& out) const;
    // edgeに近づいたときのマーカーの位置。visible=近くにedgeがある、allowed=そこで操作できる。
    struct EdgeMarker {
        bool visible = false;
        bool allowed = false;
        rs::core::Vec3 pos;
    };
    // 始点待ちの敷設ツールで、クリックすると分岐が始まる位置(OnClickScreenと同じスナップ判定)。
    EdgeMarker FindBranchMarker(float screenX, float screenY, const rs::core::Mat4& viewProj,
                                uint32_t width, uint32_t height) const;
    // 敷設ツールで、クリックすると点が置かれる位置(OnClickScreenと同じスナップ判定)。
    // edgeマーカーが出る位置(分岐の始点)は含めない。kindで何にスナップしているかが分かる。
    struct SnapCursor {
        bool visible = false;
        SnapKind kind = SnapKind::None;
        rs::core::Vec3 pos;  // レール天端の高さ
    };
    SnapCursor FindSnapCursor(float screenX, float screenY, const rs::core::Mat4& viewProj,
                              uint32_t width, uint32_t height) const;
    // Alt+クリックで分割される位置(SplitEdgeAtScreenと同じ判定)。
    EdgeMarker FindSplitMarker(float screenX, float screenY, const rs::core::Mat4& viewProj,
                               uint32_t width, uint32_t height) const;
    // Alt+クリックでedgeを分割できる編集状態か。敷設ツールの始点待ちか、
    // 特別な編集モードに入っていない選択ツールのとき。
    bool CanAltSplit() const {
        if (mode == ToolMode::PlaceArc) return !IsPlacingArc();
        return mode == ToolMode::Select && !sectionSplitMode && !stationAnchorMode &&
               !sceneryPlacementMode && !placementAddMode && !wireEditMode;
    }
    // Alt+クリックで分割する点(edgeとその弧長)を決める。edgeが無ければfalse。
    bool ResolveSplitPoint(float screenX, float screenY, const rs::core::Mat4& viewProj,
                           uint32_t width, uint32_t height, rs::world::Guid& edgeGuid,
                           double& s) const;
    // Alt+クリック用。クリック位置のedge上の点(カーブ開始点が近ければそこ)でedgeを分割する。
    bool SplitEdgeAtScreen(float screenX, float screenY, const rs::core::Mat4& viewProj,
                           uint32_t width, uint32_t height);

    // 右クリック/Esc相当。1段階戻す(終点待ち->中間点待ち->始点解除)。
    // 約束事(敷設状態機械)は TrackLayingSession.h を参照。
    void CancelPlacement() { layingSession_.CancelPlacement(*this); }

    // 現在のマウス位置から敷設プレビューを組み立てる（毎フレーム呼ぶ）。
    // 約束事は TrackLayingSession.h を参照。
    PlacePreview BuildPreview(float screenX, float screenY, const rs::core::Mat4& viewProj,
                              uint32_t width, uint32_t height) const {
        return layingSession_.BuildPreview(*this, screenX, screenY, viewProj, width, height);
    }

    // BuildPreview のワールド座標版(ヘッドレステスト用)。OnClickWorld と同じノードスナップ判定。
    PlacePreview BuildPreviewWorld(const rs::core::Vec3& worldPos, float snapRadius = 0.3f) const {
        return layingSession_.BuildPreviewWorld(*this, worldPos, snapRadius);
    }

    // ---- 敷設フロー（ワールド座標直接指定, ヘッドレステスト用） ----
    // スクリーン座標を経由せず、ワールド座標で「クリック」を模擬する。
    // 既存nodeへのスナップ判定はワールド距離(snapRadius)で行う。約束事は
    // TrackLayingSession.h を参照。
    void OnClickWorld(const rs::core::Vec3& worldPos, float snapRadius = 0.3f) {
        layingSession_.OnClickWorld(*this, worldPos, snapRadius);
    }

    // 現在敷設操作の途中か(TrackLayingSession::IsPlacing()への委譲、
    // スペース+edgeクリック開始判定用)。
    bool IsPlacingArc() const { return layingSession_.IsPlacing(); }
    // スペース+edgeクリックでの分岐敷設開始(TrackLayingSession::StartBranchFromEdge()
    // への委譲)。約束事は TrackLayingSession.h を参照。
    bool StartBranchFromEdge(const rs::world::Guid& edge, double s) {
        return layingSession_.StartBranchFromEdge(*this, edge, s);
    }

    // ---- Undo/Redo ----
    // 編集・Undo・Redoは同じ後処理を通し、成功した操作だけ世代を進める。
    bool ExecuteCommand(std::unique_ptr<ICommand> command,
                        rs::world::SceneRegistry* sceneRegistry = nullptr);
    // 読込によるグラフ全体の差し替えも、編集と同じ世代更新と参照整理を通す。
    void NotifyGraphChanged();
    // 同じGUIDを含むシーンの再読込でも、画面側の保留操作を失効させる。
    void NotifySceneReplaced() { ++sceneGeneration_; NotifyGraphChanged(); }
    uint64_t SceneGeneration() const { return sceneGeneration_; }
    void Undo();
    void Redo();
    bool IsDragging() const;
    bool CanUndo() const { return !IsDragging() && stack.CanUndo(); }
    bool CanRedo() const { return !IsDragging() && stack.CanRedo(); }

    // ---- 選択edge編集 ----
    // 成功した編集だけが共通の実行入口で世代を進める。
    void SetSelectedEdgeProperty(const EdgePropertyDelta& delta) {
        edgeProps_.SetSelectedEdgeProperty(*this, selectedEdge, delta, &manualOverlay);
    }
    // 選択中の全レールへ、レールごとに計算した差分を適用する(1回のUndo)。速度制限・カント自動など
    // 実体を持つレールの値用なので、複線中心線(centerlineOnly)は飛ばす。
    void SetSelectedEdgesProperty(const EditorEdgeProperties::EdgeDeltaFn& makeDelta) {
        const EditorEdgeProperties::EdgeDeltaFn skipCenterline =
            [&makeDelta](const rs::world::TrackEdge& edge) -> std::optional<EdgePropertyDelta> {
            if (edge.centerlineOnly) return std::nullopt;
            return makeDelta(edge);
        };
        edgeProps_.SetEdgesProperty(*this, SelectedEdges(), skipCenterline, &manualOverlay);
    }
    // 選択中の全レールの全区間から、removeを外しaddを加える(1回のUndo)。拒否・変化なしならfalse。
    bool EditFoldersOnSelectedEdges(const std::vector<std::string>& add,
                                    const std::vector<std::string>& remove) {
        return sectionEdit_.EditFoldersOnEdges(*this, SelectedEdges(), add, remove, manualOverlay);
    }
    void ToggleSelectedEdgeTrackPlugin(const std::string& folderUtf8) {
        edgeProps_.ToggleSelectedEdgeTrackPlugin(*this, selectedEdge, folderUtf8);
    }
    void SetSelectedEdgeFolderRange(const std::string& folderUtf8, double sStart, double sEnd) {
        edgeProps_.SetSelectedEdgeFolderRange(*this, selectedEdge, folderUtf8, sStart, sEnd);
    }

    // ---- edge分割(SplitEdge) ----
    // edgeGuidをsで2本へ分割する。拒否条件に該当する場合は何もせずfalseを
    // 返す(CommandStackへは積まない)。実装はEditorEditOps.cpp。UIは持たない
    // (APIとテストまで)。
    bool SplitEdgeAt(const rs::world::Guid& edgeGuid, double s);

    // ---- edge結合(MergeEdge) ----
    // sharedNodeGuidを境に接続する2本のedgeを1本へ結合する。拒否条件に該当する
    // 場合は何もせずnulloptを返す(CommandStackへは積まない)。実装はEditorEditOps.cpp。
    // Editorはワープリンクテーブル(Scene横断の独立テーブル、LayoutSession所有)を
    // 持たないため、ここではワープ判定を行わない(既知の制限。呼び出し側で別途
    // 確認する必要がある)。UIは持たない(APIとテストまで)。
    // warpLinks: 共有nodeのワープリンク判定に使う(結合はnodeを消すため必須の
    // 安全確認)。Editor自体はWarpLinkTableを所有しないため、呼び出し側(LayoutSession
    // 等がwarpLinksを持つ場面)が渡せるようnullable。nullptrなら判定をスキップする
    // (未配線のUI経路からの呼び出しは現状これに該当、既知の制限)。
    // allowLayerMismatch=trueの場合、layer不一致による拒否をスキップする(UI確認
    // ダイアログで許可済みのケース)。
    std::optional<rs::world::EdgeMergeRemap> MergeEdgeAt(
        const rs::world::Guid& sharedNodeGuid,
        const rs::world::WarpLinkTable* warpLinks = nullptr,
        bool allowLayerMismatch = false);

    // ---- edge内区間分割の編集 ----
    // 約束事は EditorSectionEdit.h を参照。UIは持たない。
    // 区間の編集は、レールを1本だけ選んでいるときのもの。複数選択中は対象なし(Nil)になる。
    rs::world::Guid SectionTargetEdge() const {
        return SelectedEdgeCount() == 1 ? selectedEdge : rs::world::NilGuid();
    }
    bool SplitSelectedEdgeSectionAt(SectionLayer layer, double s) {
        return sectionEdit_.SplitSectionAt(*this, SectionTargetEdge(), layer, s);
    }
    bool RemoveSelectedEdgeSectionBoundary(SectionLayer layer, std::size_t boundaryIndex) {
        return sectionEdit_.RemoveSectionBoundary(*this, SectionTargetEdge(), layer, boundaryIndex);
    }
    bool MoveSelectedEdgeSectionBoundary(SectionLayer layer, std::size_t boundaryIndex,
                                         double newS) {
        return sectionEdit_.MoveSectionBoundary(*this, SectionTargetEdge(), layer,
                                                boundaryIndex, newS);
    }
    bool SetSelectedEdgeSectionFolders(std::size_t sectionIndex,
                                       std::vector<std::string> folders) {
        return sectionEdit_.SetSectionFolders(*this, selectedEdge, sectionIndex,
                                              std::move(folders), manualOverlay);
    }

    void RemoveSelectedEdge(rs::world::WarpLinkTable* warpLinks = nullptr,
                            std::size_t sceneIndex = 0);
    // 選択nodeを削除する。接続edgeを全て削除し、最後のedge削除でそのnodeも
    // 自動的に取り除かれる（各edge削除はUndo対応のRemoveEdgeCommandとして積む）。
    void RemoveSelectedNode(rs::world::WarpLinkTable* warpLinks = nullptr,
                            std::size_t sceneIndex = 0);

    // ---- ノードドラッグ(press-drag-release) ----
    // 約束事(動かせる条件・不成立時の挙動)は EditorNodeDrag.h / NodeDragPlan.h を参照。
    bool CanMoveNode(const rs::world::Guid& nodeGuid) const {
        return nodeDrag_.CanMoveNode(graph, nodeGuid);
    }
    bool MoveNodeTo(const rs::world::Guid& nodeGuid, const rs::core::Vec3& newPos) {
        return nodeDrag_.MoveNodeTo(*this, manualOverlay, nodeGuid, newPos);
    }

    // ドラッグ中プレビュー(dry-run、何も変更しない)。
    NodeDragPlan PreviewMoveNode(const rs::world::Guid& nodeGuid, const rs::core::Vec3& candidatePos) const {
        return nodeDrag_.PreviewMoveNodeTo(graph, nodeGuid, candidatePos);
    }
    NodeDragPlan PreviewSetNodeTangent(const rs::world::Guid& nodeGuid,
                                           const rs::core::Vec3& candidateTangent) const {
        return nodeDrag_.PreviewSetNodeTangent(graph, nodeGuid, candidateTangent);
    }

    // 選択中ノードの接線を直接指定する(接線ハンドル確定時)。
    // newTangentは単位ベクトル(呼び出し側で正規化済みであること)を期待する。垂直(Y)成分は
    // 呼び出し側の都合(接線ハンドルは元の勾配を維持する)次第で、本関数自体はY成分を
    // 特別扱いしない。
    bool SetNodeTangent(const rs::world::Guid& nodeGuid, const rs::core::Vec3& newTangent) {
        return nodeDrag_.SetNodeTangent(*this, manualOverlay, nodeGuid, newTangent);
    }

    // ---- 分岐(ポイント)開通方向切替(方式A) ----
    // 約束事は EditorEdgeProperties.h を参照。
    bool TrySetPointActiveLink(const rs::world::Guid& nodeGuid, int newActiveIndex) {
        return edgeProps_.TrySetPointActiveLink(*this, nodeGuid, newActiveIndex);
    }

    // ---- 中心線(centerlineOnly)ノードのドラッグ ----
    // 約束事(メンバー追従の規約)は NodeDragCenterline.h を参照。
    bool CanMoveCenterlineNode(const rs::world::Guid& nodeGuid) const {
        return nodeDrag_.CanMoveCenterlineNode(graph, nodeGuid);
    }

    // press-drag-release方式(押す→ドラッグ→離す)のノード位置ドラッグ
    // (EditorViewport.cppが処理、trackCount等と同じくUndo対象外の直接mutate)。
    NodeDragUiState nodeDragUi;
    // ドラッグを中断したら、押下の記録も消す(ボタンを押したままでも再開しない)。
    void CancelNodeDrag() {
        nodeDragUi.Cancel();
        selectPress.Clear();
    }

    // 接線ハンドルのドラッグ(選択中ノードのみ表示・水平面内のみ)。
    TangentDragUiState tangentDragUi;
    void CancelTangentDrag() {
        tangentDragUi.Cancel();
        selectPress.Clear();
    }

    // 通常Selectモードで押した瞬間に掴んだ対象(ノード・接線ハンドルのドラッグ開始に使う)。
    SelectPressUiState selectPress;
    // 空き地からの矩形選択(Undo対象外のUI状態)。
    BoxSelectUiState boxSelectUi;

private:
    uint64_t sceneGeneration_ = 0;
    std::size_t placedCount_ = 0;
    std::size_t removedCount_ = 0;
    enum class CommandAction { Execute, Undo, Redo };
    bool ApplyCommand(CommandAction action, std::unique_ptr<ICommand> command = nullptr,
                      rs::world::SceneRegistry* sceneRegistry = nullptr);
    void PruneInvalidReferences();
    rs::world::SceneRegistry* sceneRegistry_ = nullptr;
    OrderedGuidSelection selectedSceneEntityIds_;
    void SyncSelectedSceneEntity() {
        selectedSceneEntityId = selectedSceneEntityIds_.Primary();
    }


public:

    // ---- 配置物(Pier/Pole)の手動編集 ----
    // 実体化は pluginSections を変更するため、実体化と編集は一つの Undo 可能コマンドとして積む。
    // 実際に変化する要求だけを積み、変化した場合にtrueを返す。
    bool ExecutePlacementEdit(std::vector<rs::world::MaterializeSnapshot> snapshots,
                              PlacementEditCommand::Request request);
    void DeleteSelectedPlacement(
        std::vector<rs::world::MaterializeSnapshot> snapshots = {});
    bool DematerializeSelectedEdgeFolder(const std::string& folderUtf8);
    // 「手動で再配置」ボタン。
    // Undo できるよう、ClearPlacementEditsCommand 経由で消す。
    void ClearPlacementEditsForEdge(const rs::world::Guid& edge) {
        ExecuteCommand(std::make_unique<ClearPlacementEditsCommand>(
                                 manualOverlay, std::vector<rs::world::Guid>{edge}));
    }
    // 複線中心線が退化メンバでカスケード削除される際、消える前にそこへアンカー
    // されていた個別編集を残存する実レールedgeへ引き継ぐために使う。
    void ReanchorPlacementEditsForEdge(const rs::world::Guid& fromEdge,
                                       const rs::world::Guid& toEdge) {
        PlacementEditCommand::Request request;
        request.kind = PlacementEditCommand::Kind::Reanchor;
        request.edge = fromEdge;
        request.newEdge = toEdge;
        ExecutePlacementEdit({}, std::move(request));
    }

    // ---- 配置物(Pier/Pole)の手動追加 ----
    // Selectモードでこれがtrueの間、Viewportのedgeクリックはnode/edge選択ではなく
    // placementAddFolderで指定した種別を1本、実体化済み配置へ追加する
    // (EditorViewport.cppが処理、trackCount等と同じくUI設定でUndo対象外)。
    bool placementAddMode = false;
    std::string placementAddFolder;

    bool stationAnchorMode = false;
    std::string stationAnchorFolder;

    // 信号を置くときに使う信号プラグインのフォルダ(空=未選択)。書き込みは
    // SignalBlockAction::SelectSignalPlugin だけ(検証に通ったときのみ)。UI設定でUndo対象外。
    std::string signalPluginFolder;
    // 信号を置くときの種別。主信号以外は閉塞に属さない表示専用の信号になる。UI設定でUndo対象外。
    rs::world::SpeedSignalKind signalKindToPlace = rs::world::SpeedSignalKind::Main;
    // 直前のSelectSignalPluginが拒否された理由(成功したら空)。パネルとブラウザの表示用。
    std::string signalPluginError;

    bool sceneryPlacementMode = false;
    std::string sceneryPlacementFolder;
    bool sceneryPlacementContinuous = false;
    float sceneryPlacementHeightOffset = 0.0f;

    // keepUpright/verticalOffsetMは自動配置(CollectTrackPluginPlacements)と同じ
    // 意味(Pier/Poleは直立、Poleは架線支持点の高さ=paired LineのTrolleyAlt+Height)。
    // プラグイン定義を知る app 層が算出して渡す必要がある(既定のfalse/0のままだと
    // PoleがLineの高さから外れて描画される)。
    void AddManualPlacement(const rs::world::Guid& edge, double s,
                            const std::string& folderUtf8, bool keepUpright = false,
                            float verticalOffsetM = 0.0f,
                            std::vector<rs::world::MaterializeSnapshot> snapshots = {});

    // ---- 配置物のedge沿い移動
    //      ＋左右自由移動(情景モードでの自由移動) ----
    // ドラッグ中の状態。selectedPlacement*の値を起点として、ドラッグしきい値超過時に
    // EditorViewport.cppがセットする(Undo対象外の直接mutateで統一)。
    // ---- edge内区間分割編集のUI選択状態(Undo対象外) ----
    // sectionLayerは「ビューポートでの分割/境界ドラッグがどの層に作用するか」を決める。
    // 区間表を持つ層はPluginSetだけ。
    SectionLayer sectionLayer = SectionLayer::PluginSet;
    // ビューポートでの分割モード(placementAddMode/wireEditModeと同じ排他モード)。
    bool sectionSplitMode = false;
    // RmlUiレール編集タブの区間編集ツール。区間選択と境界ドラッグを許可し、
    // sectionSplitModeがONのときだけ新しい境界を分割する。
    bool sectionEditMode = false;

    // ---- 区間境界ドラッグ/分割プレビュー(Undo対象外、placementDragUi等と同じUI状態) ----
    SectionEditUiState sectionEditUi;
    void CancelSectionBoundaryDrag() { sectionEditUi.CancelBoundaryDrag(); }

    // 層ごとの選択区間。直接読まずSelectedSectionIndex()を通すこと(下記)。
    std::size_t selectedSectionIndexRaw[1] = {0};

    // 選択中区間の添字を、現在の選択edgeの区間数でクランプして返す。
    // 生の添字はUndo/境界削除/Normalizeによる区間数の減少や、選択edgeの切替で
    // 簡単に古くなる(Undoは1キーで踏める)。Spans()は生のvectorを返すため、
    // 添字を直接使う表示コードは範囲外アクセスになりうる。読み出しは必ずここを
    // 通し、範囲外なら最後の区間へ丸める。区間が無い/edge未選択なら0。
    std::size_t SelectedSectionIndex(SectionLayer layer) const {
        const std::size_t raw = selectedSectionIndexRaw[static_cast<int>(layer)];
        const auto it = graph.edges.find(selectedEdge);
        if (it == graph.edges.end()) return 0;
        const std::size_t count = EditorSectionEdit::SectionCount(it->second, layer);
        if (count == 0) return 0;
        return (raw >= count) ? (count - 1) : raw;
    }
    void SetSelectedSectionIndex(SectionLayer layer, std::size_t index) {
        selectedSectionIndexRaw[static_cast<int>(layer)] = index;
    }
    // 選択edgeが変わったときに呼ぶ(添字は前のedgeの区間を指しているため)。
    void ResetSelectedSectionIndices() {
        selectedSectionIndexRaw[0] = 0;
    }

    PlacementDragUiState placementDragUi;

    // 同一edge内へ移動するAPI(クロスエッジ非対応・dir不変)。
    void MovePlacementTo(const rs::world::Guid& edge, double oldS,
                         const std::string& folderUtf8, double newS,
                         bool keepUpright = false, float verticalOffsetM = 0.0f,
                         float oldLateralOffsetM = 0.0f, float newLateralOffsetM = 0.0f,
                         std::vector<rs::world::MaterializeSnapshot> snapshots = {});

    // クロスエッジ移動対応版。newDirは移動先edge上のTrackPosition::dir
    // (PlacementDragInput.cppがホップ毎に算出、PlacementDragUiState::originDirの説明を参照)。
    void MovePlacementTo(const rs::world::Guid& oldEdge, double oldS,
                         const std::string& folderUtf8, const rs::world::Guid& newEdge, double newS,
                         bool keepUpright, float verticalOffsetM, float oldLateralOffsetM,
                         float newLateralOffsetM, int newDir,
                         std::vector<rs::world::MaterializeSnapshot> snapshots = {});

    // ---- 架線の手動接続/切断 ----
    // Selectモードでこれがtrueの間、Viewportで架線の接続点や径間をクリックして
    // 接続/切断する(下の「接続点を2つ結ぶ方式」。placementAddModeと同じくUI設定でUndo対象外)。
    bool wireEditMode = false;
    // ブラウザで選んだ架線プラグインのフォルダ。空でなければ、接続する架線はこれを使う
    // (空なら結ぶ柱の組から決める)。モードを抜けても保持する。
    std::string wireLineFolder;

    // ---- 架線の接続点を2つ結ぶ方式(wireEditModeの中、Undo対象外のUI状態+Undo対象の操作) ----
    // 1点目に選んだ接続点。2点目を確定するかEsc/右クリックで空に戻る。
    std::optional<WirePointRef> wireFirstPoint;
    // 1点目が無い間のホバー中・選択中の径間(切断の対象)。選択はwireSelectedRevisionの時点の
    // 配置物の構成に対するもので、revisionが進んだら入力側が解除する。
    std::optional<WirePointRef> wireHoveredPoint; // カーソルが乗っている接続点(1点目と同じ点は除く)
    std::optional<WireSpanRef> wireHoveredSpan;
    std::optional<WireSpanRef> wireSelectedSpan;
    std::uint64_t wireSelectedRevision = 0;
    void ClearWireSpanSelection() {
        wireHoveredSpan.reset();
        wireSelectedSpan.reset();
    }
    // 操作を拒否した理由を、敷設の拒否と同じ表示経路(パネルの警告)へ出す。reasonは文字列リテラル。
    void NoteOperationRejected(const char* reason) { layingSession_.NoteOperationRejected(reason); }
    void ClearOperationRejected() { layingSession_.ClearOperationRejected(); }
    // 2点を結ぶ(同じ2点の既存径間は置き換える)。実体化が要る柱はsnapshotsで同じコマンドに含める。
    // 何も変わらない要求(同じ点どうし等)はUndoに積まずfalseを返す。
    bool ConnectWireSpan(const WirePointRef& pointA, const WirePointRef& pointB,
                         const std::string& lineFolderUtf8,
                         std::vector<rs::world::MaterializeSnapshot> snapshots = {});
    // 2点を結ぶ径間を1本だけ切る。
    bool DisconnectWireSpan(const WirePointRef& pointA, const WirePointRef& pointB,
                            std::vector<rs::world::MaterializeSnapshot> snapshots = {});

    bool SetSelectedEdgeTransition(rs::world::AlignmentSegment::TransitionShape shape,
                                   float newTransLen) {
        return edgeProps_.SetSelectedEdgeTransition(*this, manualOverlay, selectedEdge, shape,
                                                    newTransLen);
    }

    // ---- 標高編集 ----
    // 選択nodeの標高(pos.y)だけを変える。ノードドラッグと同じ計画(PlanNodeElevation)で、
    // 形を作れないedgeがあれば何も変えない。複線中心線のノードならメンバーも追従する。
    void SetSelectedNodeElevation(float elevation) {
        nodeDrag_.SetNodeElevation(*this, manualOverlay, selectedNode, elevation);
    }

    // ---- 既存ノード複数選択による複線開始 ----
    // 未placing状態でノードにスナップしたクリックのたび選択列へトグル追加/解除
    // される([0]=基準線)。app/uiのオーバーレイがハイライトに使う。約束事は
    // TrackLayingSession.h を参照。
    const std::vector<rs::world::Guid>& SelectedStartNodes() const {
        return layingSession_.SelectedStartNodes();
    }

    // 最後のノード開始試行(地面クリックでの検証)が拒否されたか。UIパネル表示用。
    bool StartSelectionRejected() const { return layingSession_.StartSelectionRejected(); }
    const char* StartSelectionRejectReason() const {
        return layingSession_.StartSelectionRejectReason();
    }

    // 直近の確定が拒否された理由。拒否されていなければ空。
    const std::string& CommitRejectReason() const { return layingSession_.CommitRejectReason(); }

    // ---- 複線×S字接続: 終点ノードの明示複数選択 ----
    const std::vector<rs::world::Guid>& SelectedEndNodes() const {
        return layingSession_.SelectedEndNodes();
    }
    int ActiveTrackCount() const { return layingSession_.ActiveTrackCount(); }
    bool ExistingNodeStart() const { return layingSession_.ExistingNodeStart(); }

private:
    // TrackLayingSession が Editor の private メンバ(graph/stack/revision/
    // defaultTrackPlugins/trackCount/trackSpacing/transShape/
    // CurrentTransitionSpec/PickNode/FindNearestNodeWorld等)へ広く依存する
    // ため、back-referenceを持たせない代わりにfriendとする(設計方針は
    // TrackLayingSession.h を参照)。
    friend class TrackLayingSession;

    // 主(最後に選んだ1本)が変わったら写しを更新し、前のedgeの区間を指す添字を戻す。
    void SyncSelectedEdge() {
        const rs::world::Guid primary = edgeSelection_.Primary();
        if (!(primary == selectedEdge)) ResetSelectedSectionIndices();
        selectedEdge = primary;
    }
    EdgeSelection edgeSelection_;

    // ノードドラッグ操作の実装本体(無状態)。
    EditorNodeDrag nodeDrag_;
    // 選択edge/nodeプロパティ操作の実装本体(無状態)。
    EditorEdgeProperties edgeProps_;
    // edge内区間分割の編集操作の実装本体(無状態)。
    EditorSectionEdit sectionEdit_;
    // 円弧/複線の敷設状態機械の実装本体。約束事は TrackLayingSession.h を参照。
    TrackLayingSession layingSession_;


    // 円弧ツール用クリック処理(3点指定)。hitPos=地面ヒット or スナップnode座標。
    // 実体は TrackLayingSession::HandleClickWorldImplArc(*this, ...) への委譲。
    void HandleClickWorldImplArc(const rs::core::Vec3& hitPos, const rs::world::Guid& snappedNode) {
        layingSession_.HandleClickWorldImplArc(*this, hitPos, snappedNode);
    }

    // 全node中、posから最も近いものをsnapRadius以内で探す。
    rs::world::Guid FindNearestNodeWorld(const rs::core::Vec3& pos, float snapRadius) const;
};

} // namespace rs::editor

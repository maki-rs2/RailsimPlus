#pragma once

// RunFrameLoop()の実装を複数TUへ分割するための内部専用ヘッダ。
// AppRuntime.h(main.cppから広く波及する)には入れず、AppRuntime.cpp /
// AppRuntimeFrameUi.cpp / AppRuntimeFrameSim.cpp / AppRuntimeFrameRender.cpp の
// 4TUだけがincludeする(src/app/scene/SceneSlotSyncInternal.hと同じパターン)。

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "imgui.h"

#include "AppRuntime.h"
#include "../action/PlacementHeightAction.h"
#include "../action/SceneryYawAction.h"
#include "../camera/PhotoAutoIso.h"
#include "../diagnostics/FrameProfiler.h"
#include "../input/InputSource.h"
#include "../diagnostics/SceneCycleSmokeDriver.h"
#include "../diagnostics/SwitchFlipSmokeDriver.h"
#include "../gpu/App.h"
#include "../scene/SceneTrainController.h"
#include "../ui/PluginSettingsViewportLink.h"
#include "../ui/SignalBlockViewportLink.h"
#include "../ui/UiPanels.h"
#include "ScreenshotCapture.h"
#include "CameraOverride.h"
#include "CameraPhotoCapture.h"
#include "LayoutDocumentController.h"
#include "WorkspaceEditorBridge.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../../render/postprocess/OutputRect.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../world/time/MoonPosition.h"
#include "../../world/time/SolarPosition.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// 保存されていない変更がレイアウトにあるか(自動復元の保存とデバイスロスト時のバックアップが同じ判定を使う)。
inline bool HasUnsavedLayout(const AppRuntimeGlobals& globals, const AppRuntimeState& state) {
    return state.layoutDocument.IsDirty(globals.editor.DocumentRevision(), state.sceneRegistry.PersistentRevision());
}

// ループ横断で生き続ける状態。RunFrameLoop()のwhileループの外で構築し、参照で
// 各ステージへ持ち回る(1フレームで死ぬ作業用のFrameScratchとは寿命が異なるため
// 混ぜない)。
struct FrameLoopState {
    AppRuntimeGlobals& globals;
    AppRuntimeState&   state;
    ImGuiIO&           io;
    const rs::app::input::InputSource& input;

    double fixedTickAccumulator = 0.0;
    ViewMode viewMode;
    int    frame = 0;
    CameraOverrideState cameraOverride;
    int placedSoundDumpFrame = 0;
    uint64_t placedSoundSceneGeneration = UINT64_MAX;
    // 遅延した起動時読込を実行するまでに描いたフレーム数。ウィンドウと「読込中」
    // 表示を最低1フレーム出してから読み込む(そうしないと読込中は未描画の白窓に
    // なり、ウィンドウを先に出す意味が無い)。この助走フレームは --smoke の
    // フレーム数に数えない(「読込後にNフレーム描く」意味を保つため)。
    int    framesBeforeStartupLoad = 0;
    // 前のフレームの終わりに読込ジョブ(SceneRenderer::SyncJobActive)が動いていたか。ジョブが
    // 終わったフレームでは、このあとTickEditorViewportが止めていた静的配置のインスタンスバッチを
    // まとめて作り直し、大規模レイアウトでは数秒メインスレッドが止まる。その間に鳴り始めた音が
    // 鳴りっぱなしになるので、作り直しが済むまで配置物の音を鳴らさない(AppRuntimeFrameSim.cpp)。
    bool   syncJobActiveLastFrame = false;
    // フレーム時間の揺れ調査用(RS_PROFILE_FRAME指定時のみ記録。
    // vsync予算超過フレームとゲーム内固定tick実行回数の相関確認)。
    FrameProfiler frameProfiler;

    // ファインダーの出入りの記憶(ResolveFrameFieldOfView が使う)。
    bool photoFinderActive = false;
    // ファインダーへ入るとき入力欄へ書いた焦点距離[mm]。出るときに、ユーザーが変えたかの判定に使う。
    double photoFinderEntryFocalMm = 0.0;

    // ISO オート(ResolvePhotoRender・CollectPhotoMeter が使う)。測光した画面の輝度(読み戻しのたびに入り、
    // 次の更新で取り込む)と、決まった ISO の追従の状態。測光が要らない間は捨てる。
    AutoIsoState photoAutoIso;
    std::optional<double> photoMeteredLuminance;
    // RS_AUTOISO のログ(最初の測光と、そのあと一定の間隔だけ出す)。
    bool photoAutoIsoLogged = false;
    double photoAutoIsoLogSeconds = 0.0;

    SwitchFlipSmokeDriver switchFlipSmoke;
    SceneCycleSmokeDriver sceneCycleSmoke;

    // F12の3Dだけスクリーンショット(撮影待ちの状態を持つ)。
    ScreenshotCapture screenshot;
    CameraPhotoCapture cameraPhoto;
    CameraFocusProbe cameraFocus;

    // ↑↓キーとCtrl+ホイールの高さ調整(キー長押しの状態を持つので、ループ横断でここに置く)。
    PlacementHeightAction placementHeight;
    SceneryYawAction sceneryYaw;

    // --rmlui-workspaceの画面状態と編集ツールのつなぎ。
    WorkspaceEditorBridge workspaceBridge;
    // --rmlui-workspaceの主メニューの保存・ロード・終了。
    LayoutDocumentController layoutDocument;
    // プラグイン設定パネルとビューポートの受け渡し(WorkspaceEditorBridge::TickとTickEditorViewportが使う)。
    PluginSettingsViewportLink pluginSettingsLink;
    SignalBlockViewportLink signalBlockLink;
    // ImGui画面で、クリックで開いているプラグイン設定ウィンドウの対象の個体ID(閉じていれば空)。
    // エディタの選択(selectedSceneEntityId)とは別の状態。RmlUi画面では使わない(画面状態が持つ)。
    rs::world::Guid imguiPluginSettingsTarget = rs::world::NilGuid();
    // --rmlui-workspaceのプラグインブラウザの裏側(カタログ・走査・画面状態)。走査のワーカーを持つので、
    // RunFrameLoopを抜けるとき(FrameRendererとRmlUiの終了より前)に壊れる位置=ここに置く。
    std::optional<rs::app::plugin_browser::PluginBrowserService> pluginBrowser;

    FrameLoopState(AppRuntimeGlobals& g, AppRuntimeState& s, ImGuiIO& imguiIo, const rs::app::input::InputSource& inputSource, ViewMode initialViewMode,
                   bool switchFlipSmokeActive)
        : globals(g), state(s), io(imguiIo), input(inputSource), viewMode(initialViewMode),
          switchFlipSmoke(switchFlipSmokeActive), sceneCycleSmoke(s.cli.sceneCycleSmokeSwitches),
          placementHeight(g.editor) {
        if (s.cli.rmluiWorkspace) {
            // サムネイルの撮影は本編と同じデバイス・PSO・トーンマップを使う。FrameRenderer(g.app)は
            // このループの状態より長く生きる。
            pluginBrowser.emplace(rs::rhi::Device(g.app.Device()), &g.app.MeshPipelineShared(), &g.app.Tonemap());
        }
    }
};

// 1フレームで死ぬ作業用の状態。ループ本体の先頭で毎イテレーション新しいローカルと
// して構築する(メンバに持たせてステージがリセットする形にはしない。「クリアし
// 忘れ」を構築の仕組みで防ぐため)。
struct FrameScratch {
    std::chrono::steady_clock::time_point tCpuFrame0;
    double gameDtPerTick = 0.0;
    int    gameTicksThisFrame = 0;
    rs::world::SolarState sunState;
    // 太陽の通り道の緯度[度](現在のEnvのLatitude、無ければ環境プリセット)。AdvanceSimulationの
    // 冒頭で1回だけ決め、太陽位置・昼夜判定・季節スイッチの全呼び出しが同じ値を使う。
    double solarLatitudeDeg = 0.0;
    float  eye[3] = {0.0f, 0.0f, 0.0f};
    // eyeと対になる視線方向(正規化済み)。カブ視点が有効な間はeye同様、俯瞰カメラでは
    // なくCabinViewCamera::TryResolveEyeForwardの値になる(AdvanceSimulation参照)。
    float  forward[3] = {0.0f, 0.0f, 1.0f};
    std::function<rs::world::TrackGraph*(size_t)> graphForScene;
    int    sceneTicksThisFrame = 0;
    double sceneDtPerTick = 0.0;
    CameraView view{};
    // このフレームの実効の縦の視野角[rad]。フレームの先頭で ResolveFrameFieldOfView が1回だけ決め、
    // LOD・_CAMDIST・ヘッドライト・車輪音のスナップショットと描画が同じ値を読む。
    float fieldOfView = 0.0f;
    size_t recomputedSlotsThisFrame = 0;
    double trainTickMsThisFrame = 0.0;
    double sceneryTickMsThisFrame = 0.0;
    double trainSearchMsThisFrame = 0.0;
    double trainKinematicsMsThisFrame = 0.0;
    double carPartWorldMsThisFrame = 0.0;
    double trainRuntimeMsThisFrame = 0.0;
    double trainSubmitMsThisFrame = 0.0;
    double carBatchMsThisFrame = 0.0;
    double carFallbackMsThisFrame = 0.0;
    double sceneTickMs = 0.0;
    double cpuFrameMs = 0.0;

    // sceneTicksThisFrame回の SceneRenderer::Tick 呼び出し分の統計を集約する。
    void Accumulate(const SceneTrainController::TickStats& stats) {
        trainSearchMsThisFrame += stats.searchMs;
        trainKinematicsMsThisFrame += stats.kinematicsMs;
        carPartWorldMsThisFrame += stats.partWorldMs;
        trainRuntimeMsThisFrame += stats.runtimeMs;
        trainSubmitMsThisFrame += stats.submitMs;
        carBatchMsThisFrame += stats.batchMs;
    }
};

// --- AppRuntimeFrameUi.cpp ---

// フレームループ先頭のサウンド更新+フォントアトラス再構築(NewFrame三点セットより前で
// なければならない、詳細は各所コメント参照)。
void RunPluginTickAndFontRebuild(FrameLoopState& loopState);

// 遅延した起動時読込の実行(初回1助走フレーム後)。
void RunDeferredStartupLoadIfDue(FrameLoopState& loopState);

// レイアウト読込予約(LayoutPersistenceAction::RequestLoadLayout)の実行。
// 「読込中」インジケータが1回でも描画された予約があれば、ここでLoadLayout()を
// 実行しログ/パネルのエラー表示へ結果を反映する(起動時CLI・ボタン経由で共通)。
void TickPendingLayoutLoad(FrameLoopState& loopState);

// 読込中/進捗ウィンドウ・プラグイン情報パネル・表示切り替えパネル・モード別
// パネル群の描画。
void DrawFrameUiPanels(FrameLoopState& loopState);

// 遅延した起動時読込の実行直前に出す「読込中」ウィンドウ(読込予約が残っている間だけ)。
void DrawDeferredStartupLoadNotice(const AppRuntimeState& state);

// Ctrl+上下キーによるレイアウトのScene切替。ImGuiパネルの有無によらず毎フレーム呼ぶ。
void HandleSceneSwitchHotkey(FrameLoopState& loopState);

// Ctrl+Z(Undoの割当)で、直近に削除したシーンを戻す。今のシーンのEditorに取り消せる編集があるときは、
// EditorのUndoを優先し、ここでは何もしない。編集コンテキストの外(シーン画面・鑑賞)でも受け付ける。
void HandleSceneRestoreHotkey(FrameLoopState& loopState);

// シーンのホットキー(切り替えと、削除の取り消し)をまとめて処理する。毎フレーム呼ぶ。
void HandleSceneHotkeys(FrameLoopState& loopState);

// --- AppRuntimeFrameFov.cpp ---

// 実効の視野角を決めて scratch.fieldOfView へ入れる。フレームの先頭(SyncAndTickScene より前)で1回呼ぶ。
// ファインダー中は撮影カメラの焦点距離とファインダーの縦横比から、そうでなければ FlyCamera::fieldOfView。
// ファインダーへ入る瞬間は fieldOfView に近い焦点距離の段を入力欄へ入れ、出る瞬間は、ユーザーが焦点距離を
// 変えていれば fieldOfView へ書き戻す。ファインダー中は Shift+ホイールの視野角変更を止める。
void ResolveFrameFieldOfView(FrameLoopState& loopState, FrameScratch& scratch);

// --- AppRuntimeFramePhoto.cpp ---

// フォトモード(ファインダー)の、描画へ渡す入力。ファインダーが出ていなければ、値はすべて空・偽。
struct PhotoRenderInputs {
    std::optional<rs::render::OutputRect> finderRect;               // ファインダーの枠(バックバッファのピクセル座標)
    std::optional<rs::render::DepthOfFieldCamera> dofCamera;        // 撮影カメラの被写界深度の値
    std::optional<float> exposureMultiplier;                        // トーンマップの露出へ掛ける撮影カメラの倍率
    std::optional<float> shutterSeconds;                            // モーションブラーの長さを連動させるシャッター速度[秒]
    bool luminanceMeter = false;                                    // ISO オートの測光を走らせる
};

// App::Render の直前に呼ぶ。ファインダーの枠と撮影カメラの値を求め、ISO オートの追従を進めて決まった ISO を
// 露出の倍率と7セグ表示へ反映し、scratch.view の縦横比と視野角を決める。
PhotoRenderInputs ResolvePhotoRender(FrameLoopState& loopState, FrameScratch& scratch);

// App::Render の直後に毎回呼ぶ。測光の結果が読み戻せていれば、画面の輝度にして次の追従で使えるよう覚える。
void CollectPhotoMeter(FrameLoopState& loopState);

// --- AppRuntimeFrameSim.cpp ---

// GameClockの固定tick進行・太陽位置再計算・システムスイッチ更新・
// ChangeTexture/ChangeMaterial/SetAnimation等の実行時反映。
void AdvanceSimulation(FrameLoopState& loopState, FrameScratch& scratch);

// SceneRegistryとの差分反映(Sync)・switch-flip-smokeの計測/切替・
// sceneTickループ(列車物理+可動部アニメ)・時間駆動アニメ反映・
// SubmitTrainRenderSnapshot。
void SyncAndTickScene(FrameLoopState& loopState, FrameScratch& scratch);

void UpdateFrameParticles(FrameLoopState& loopState, bool loading);

// --- AppRuntimeFrameRender.cpp ---

// WASD移動・カメラビュー構築・敷設エディタのTick(ビューワーモードでは無効)。
void UpdateCameraAndEditor(FrameLoopState& loopState, FrameScratch& scratch);

// スモーク/PNGキャプチャ判定・App::Render・キャプチャ結果評価・PNG保存・
// cpuFrameMs算出・FrameProfiler::Record。
void RenderFrameAndCapture(FrameLoopState& loopState, FrameScratch& scratch);

// --smoke の終了時に、--save-layout と --layout-digest の出力を書く(指定があるものだけ)。
void WriteSmokeArtifacts(AppRuntimeGlobals& globals, AppRuntimeState& state);

// 遅延起動読込待ちで助走フレームを消費すべきかを判定する(trueならオーケストレータ
// 側でframesBeforeStartupLoadを加算しcontinueする)。frame/switchFlipSmokeの終了
// 判定は含まない(この関数の後、オーケストレータ側でそのまま行う)。
bool ShouldSkipStartupLoadFrame(FrameLoopState& loopState);

} // namespace rs::app

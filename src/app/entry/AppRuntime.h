//
// AppRuntime.h - main()の初期化/フレームループ/終了処理を束ねる状態と関数群
//
// main.cppの巨大化を防ぐため、
// int main()本体から「ウィンドウ生成〜.rs2読込までの初期化」「毎フレームの
// メッセージポンプ〜描画」「終了処理」の3フェーズを分離する。
// main()は CLI解析とヘッドレス早期終了分岐(--testx等、ウィンドウ/デバイスを
// 作らない別経路)だけを残した薄いディスパッチャになる。
//
// AppRuntimeGlobals はmain.cpp側のnamespace-scopeグローバル(g_app/g_camera等)
// への参照束、AppRuntimeState はアプリ全体の実体(ウィンドウ・
// セッション・各種マネージャ)の所有者。いずれも呼び出し側(main)が実体を持ち、
// 3関数へ参照で渡す。
//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "imgui.h"

#include "AppCommandLine.h"
#include "MainWindow.h"
#include "OpenedPlugin.h"
#include "../camera/CameraControl.h"
#include "../camera/CabinViewCamera.h"
#include "../action/LayoutPersistenceAction.h"
#include "../audio/UiSoundPlayer.h"
#include "../input/InputSource.h"
#include "../locale/Localization.h"
#include "../plugin_session/ActiveEnv.h"
#include "../plugin_session/EnvCubeController.h"
#include "../plugin_session/FlareTextureSlots.h"
#include "../../render/particle/ParticlePass.h"
#include "../plugin_session/SunFlareController.h"
#include "../scene/HeadlightCollector.h"
#include "../../sim/particle/ParticleSystem.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/PluginSession.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/LayoutDocumentState.h"
#include "../plugin_session/AutoRecovery.h"
#include "../plugin_session/Rs2SceneApplier.h"
#include "../plugin_session/StartupAssetLoad.h"
#include "../plugin_session/ViewerState.h"
#include "../rmlui/RmlUiIntegration.h"
#include "../scene/TrackPluginPlacement.h"
#include "../scene/SceneRenderer.h"
#include "../settings/UserSettings.h"
#include "../update/UpdateChecker.h"
#include "../update/UpdateDownloader.h"
#include "../update/UpdateStartup.h"
#include "../ui/EditorViewport.h"
#include "../ui/ModePanels.h"
#include "../ui/UiPanels.h"
#include "../../platform/file/StableFileWatcher.h"
#include "../../sim/audio/AudioEngine.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/EnvironmentPresets.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"

namespace rs::editor { class Editor; }

namespace rs::app {

class App;

// main.cpp側のnamespace-scopeグローバルへの参照束。3関数(Initialize/RunFrameLoop/
// Shutdown)いずれもこれを受け取る。実体はmain()が所有し続ける(EditorViewportContext
// と同じ、参照で橋渡しするだけの薄い構造体)。
struct AppRuntimeGlobals {
    App&                                       app;
    CameraController&                          camera;
    CabinViewCamera&                           cabinCamera;
    rs::world::GameClock&                      gameClock;
    rs::world::SolarOverrides&                 solarOverrides;
    rs::editor::Editor&                        editor;
    uint64_t&                                  editorMeshRevision;
    uint64_t&                                  trackPluginEntryRevision;
    uint64_t&                                  sceneRegistrySceneryRevision;
    uint64_t&                                  sceneRegistryStructureRevision;
    std::vector<TrackPluginPlacementItem>&      trackPluginPlacementCache;
    WireEditCache&                            wireEditCache;
    std::vector<ImportedWireSpan>&             importedWireSpans;
    rs::editor::PlacePreview&                   lastArcPreview;
    ImVector<ImWchar>&                         fontRanges;
};

// アプリ全体のオブジェクト群の所有者。
struct AppRuntimeState {
    AppCommandLine cli;
    bool           viewerMode = false;
    // 起動時に指定したレイアウト(--openlayout)の結果をまだ受け取っていないか。同期読込でも予約でも、
    // 最初に終えた読込がその結果になる。
    bool           startupLayoutPending = false;
    // 起動時に指定したレイアウトの読込に失敗したか。立っていれば終了コードを0以外にする。
    bool           startupLayoutFailed = false;
    // ビューアーモードのライトと太陽の上書き(viewerMode が偽のときは使わない)。
    ViewerState    viewer;

    MainWindow mainWindow;
    rs::app::input::InputSource input;

    rs::sim::AudioEngine    audioEngine;
    UiSoundPlayer           uiSound;
    // 操作の効果音を、数の増減から決めるための前フレームの値。
    struct UiSoundWatch {
        std::size_t placedTrains = 0;
        std::size_t editorPlaced = 0;
        std::size_t editorRemoved = 0;
        std::size_t warnings = 0;
        std::size_t errors = 0;
    };
    UiSoundWatch            lastUiSound;
    std::vector<std::string> loadLog;
    Localization localization;
    UserSettingsData userSettings;
    UpdateChecker updateChecker;
    UpdateDownloader updateDownloader;
    // 更新のための終了(未保存の確認つき)を頼む。フレームループの間だけ入っている。
    std::function<void()> requestRestartForUpdate;
    LayoutDocumentState layoutDocument;
    AutoRecoverySchedule autoRecoverySchedule;
    std::filesystem::path autoRecoveryRoot;
    bool previousRunCrashed = false;
    std::vector<AutoRecoveryCandidate> autoRecoveryCandidates;
    // 異常終了マーカーを置いて消すか(ワークスペースの通常の起動だけ)。
    bool tracksCrashMarker = false;
    // デバイスロストで終了する手順を済ませたか。立ったあとはフレームを回さず、異常終了マーカーも消さない
    // (次回の起動でバックアップの復元を出すため)。
    bool deviceLostExit = false;
    // 最初のフレームを描き終えたか。これより前に例外で抜けたら「起動できませんでした」を出す。
    bool firstFrameRendered = false;
    // 終了後に、staging へ用意した新しい版へ差し替えて再起動するか(ShutdownAppRuntime の後に main が行う)。
    // 差し替えの入力(staging と版)は update に入れておく。
    bool restartForUpdate = false;
    // 更新の起動時の判定の結果(前回のロールバック・今回の成功)と、差し替えの入力。
    UpdateSessionState update;
    // 起動時に復元の画面を出すか。
    bool recoveryChoicePending = false;

    // 起動時に開いたプラグイン(ビューアー・--openplugin)の状態。
    OpenedPluginState openedPlugin;
    // 開いたプラグインの定義ファイル(ビューアーの定義txtのフルパス)。指定が無ければ空。
    std::string forceDefFile;

    PluginSelector           pluginSelector;
    ModePanels               modePanels;
    rs::world::SceneRegistry sceneRegistry;
    SceneRenderer            sceneRenderer;
    // .rs2の複数Scene。Ctrl+上下キー切替(RunFrameLoop)と
    // セーブデータパネルのPrev/Nextボタンが共有する。
    LayoutSession            layoutSession;
    std::optional<LayoutPersistenceAction> layoutAction;

    StartupSmokeReady smokeReady;

    // --rmlui-proto/--rmlui-workspace専用。UsesRmlUi(cli)がfalseの間は
    // Init()を呼ばない(通常起動・既存のヘッドレススモークに影響を与えない)。
    rs::app::rmlui::RmlUiIntegration rmlUiIntegration;

    // 起動時アセット読込をフレームループへ遅延するための保留情報。
    // 読込(CPUパース+メッシュ/テクスチャのGPUアップロード)は編成プラグインで
    // 1秒前後かかり、初期化中に同期実行するとその間ウィンドウが出ない。先に
    // ウィンドウと「読込中」表示を1フレーム描いてから実行することで、体感の
    // 待ち時間をD3D12初期化ぶん(約0.6秒)まで縮める。診断系CLI(スモーク/
    // PNGキャプチャ)は読込済みを前提にフレーム0から検証するため遅延しない。
    struct DeferredStartupLoad {
        bool        pending = false;
        std::string openPluginPath;
    };
    DeferredStartupLoad deferredStartupLoad;

    rs::world::EnvironmentPresets       environmentPresets;
    const rs::world::EnvironmentPreset* activePreset = nullptr;
    // アクティブSceneのEnvプラグイン。いまは緯度だけを
    // 太陽の通り道に反映する。Envなしのときは activePreset の値のまま。
    ActiveEnv                           activeEnv;
    // 現在のEnvの焼き済みキューブ一式の非同期読込とGPUへの反映。ワーカースレッドを持つ。
    EnvCubeController                   envCubeController;
    // 現在のEnvの太陽のレンズフレアと白飛びの設定と、テクスチャ要素の画像のGPUへの反映。
    SunFlareController                  sunFlareController;
    // レンズフレアの画像のスロットの台帳(太陽と灯具で共用)と、描く灯具の収集。
    FlareTextureSlots                   flareTextureSlots;
    HeadlightCollector                  headlightCollector;
    rs::sim::ParticleSystem              particleSystem;
    TextureSlotRegistry                 particleTextureSlots{rs::render::kMaxParticleTextures};
    // render_tuning.json の保存を見つける監視器。Debugビルド(RS_TUNING_HOT_RELOAD)のフレームループが
    // 最初のフレームで作る。Releaseでは作らない。
    std::optional<rs::platform_file::StableFileWatcher> renderTuningWatcher;

    explicit AppRuntimeState(std::string forceDefFile) : forceDefFile(std::move(forceDefFile)) {}
    // InitializeAppRuntimeが構築した後のみ有効(フレームループはその後にしか回らない)。
    LayoutPersistenceAction& LayoutAction() { return *layoutAction; }
};

// ウィンドウ生成〜D3D12/ImGui初期化〜起動時アセット読込〜.rs2読込までを行う
// 失敗時はfalseを返し、呼び出し側は終了コード1で
// 抜けること(ShutdownAppRuntimeは呼ばなくてよい、失敗経路はいずれも該当リソースを
// 未生成のまま返すため)。
bool InitializeAppRuntime(AppRuntimeGlobals& globals, const AppCommandLine& cli, bool viewerMode,
                          const std::string& openPluginPath,
                          AppRuntimeState& state);

// メインループ本体(while(mainWindow.PumpMessages()))。WM_QUITで戻る。
void RunFrameLoop(AppRuntimeGlobals& globals, AppRuntimeState& state);

// オーディオ/グラフィックス/ウィンドウの終了処理。
void ShutdownAppRuntime(AppRuntimeGlobals& globals, AppRuntimeState& state);

} // namespace rs::app

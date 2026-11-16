//
// RailsimPlus - .x ビューア（共通ブロックパーサ + .x テキストパーサ + 基本描画）
//
// 使い方:
//   RailsimPlus.exe                通常起動（エディタモード）
//   RailsimPlus.exe <定義.txt>     プラグインビューワーモード（txt関連付け起動用。
//                                  親フォルダをプラグインとして読込み、PI情報と
//                                  カスタムスイッチのみのUIで表示する）
//   RailsimPlus.exe --smoke N      N フレーム描画して自動終了（ビルド検証）
//   RailsimPlus.exe --open <path>  起動時に .x を読み込む
//   RailsimPlus.exe --testx <path> ヘッドレスで .x を読み統計を出力して終了
//   RailsimPlus.exe --testpi <path> ヘッドレスで txt を AST 化して終了
// (線形カーネル/メッシュ生成/敷設エディタの数値検証は RailsimPlusTests(Catch2)へ移行済み)
//
#include <windows.h>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

#include "../../platform/text/Encoding.h"
#include "../../platform/EnvironmentVariable.h"
#include "../../world/scene/SceneRegistry.h"
#include "AppCommandLine.h"
#include "AppRuntime.h"
#include "StartupFailureDialog.h"
#ifdef RS_TUNING_HOT_RELOAD
#include "RenderTuningHotReload.h"
#endif
#include "../camera/CameraControl.h"
#include "../diagnostics/CompatScan.h"
#include "../diagnostics/PluginTools.h"
#include "../diagnostics/Rs2Spike.h"
#include "../diagnostics/ThumbnailCapture.h"
#include "../diagnostics/TonemapCheck.h"
#include "../gpu/App.h"
#include "../plugin_session/Rs2SceneApplier.h"
#include "../scene/TrackPluginPlacement.h"
#include "../ui/EditorViewport.h"

using rs::app::App;
using rs::app::RunTestX;
using rs::app::RunTestPi;
using rs::app::RunCompatTest;
using rs::app::RunTestPlugin;
using rs::app::RunRs2Spike;

namespace {

App g_app;

// 日本語フォントのグリフ範囲(ImFontGlyphRangesBuilder::BuildRanges の出力)。
// フォントのビルドが完了するまで生存させる必要があるため、フレームスコープ
// より広い外側スコープ(namespace 内)に置く。
ImVector<ImWchar> g_fontRanges;

// カメラ(オービット/フライ両カメラ+入力状態+WASD更新をCameraControllerに集約)。
rs::app::CameraController g_camera;
// 運転台視点カメラ。俯瞰カメラとは別系統。
rs::app::CabinViewCamera g_cabinViewCamera;

// ゲーム内時刻(可変dtのまま進行。固定tick化は別途)。
rs::world::GameClock g_gameClock;
// .rs2 Time ブロックの EarthRotation/EarthRevolution(自転/公転の強制モード)。
// 既定はAuto(実際の時刻/季節を使う)。DecodeRs2経由で.rs2読込時に設定される。
rs::world::SolarOverrides g_solarOverrides;

// 敷設エディタ本体。
rs::editor::Editor g_editor;
uint64_t g_editorMeshRevision = ~uint64_t(0); // 最後にメッシュ再構築した時点のrevision
uint64_t g_trackPluginEntryRevision = ~uint64_t(0); // 最後にレール系リストを反映した時点のrevision
uint64_t g_sceneRegistrySceneryRevision = ~uint64_t(0); // 最後に静的SceneryをGPUインスタンス化した時点のrevision
uint64_t g_sceneRegistryStructureRevision = ~uint64_t(0); // 同、構造(スイッチ値以外)のrevision
// 自動配置物(Pier/Pole)ピッキング用キャッシュ。
// TickEditorViewportがneedRebuild時に更新する。
std::vector<rs::app::TrackPluginPlacementItem> g_trackPluginPlacementCache;
// 架線の接続点と径間のキャッシュ。
rs::app::WireEditCache g_wireEditCache;
// .rs2インポート由来の架線スパン一覧(DecodeRs2が解決)。
std::vector<rs::app::ImportedWireSpan> g_importedWireSpans;

// 円弧敷設パネル表示用: 直近フレームで計算したプレビュー(main.cppの描画ブロック
// で更新し、パネル側のR/中心角/弧長テキスト表示に使う)。
rs::editor::PlacePreview g_lastArcPreview;

// モデル/パーツ読込時、オービット・フライ両方のカメラをAABBへフィットさせる。
void FitCamerasToAabb(const float mn[3], const float mx[3]) {
    g_camera.FitToAabb(mn, mx);
    g_camera.SnapToTarget();
}

// COM初期化のRAIIガード。main()内の複数の早期returnパスでも
// CoUninitializeの呼び忘れ/不均衡が起きないようにする
// (CoInitializeExがSUCCEEDEDした場合のみ、デストラクタでCoUninitializeを呼ぶ。
// RPC_E_CHANGED_MODEの場合は自スレッドがCOM参照を取得していないため呼ばない)。
class ComInitGuard {
public:
    explicit ComInitGuard(HRESULT hr) : shouldUninitialize_(SUCCEEDED(hr)) {}
    ~ComInitGuard() {
        if (shouldUninitialize_) CoUninitialize();
    }
    ComInitGuard(const ComInitGuard&) = delete;
    ComInitGuard& operator=(const ComInitGuard&) = delete;

private:
    bool shouldUninitialize_;
};

} // namespace

int main(int argc, char** argv) {
    // コンソール出力コードページをUTF-8にする(内部文字列は全てUTF-8正規化方針の
    // ため、既定のANSI/OEMコードページのままだとspdlogの日本語ログが文字化けする)。
    SetConsoleOutputCP(CP_UTF8);

    // COM初期化(プロセスで一度)。音声バックエンド(WASAPIデバイス列挙)が
    // 起動直後に必要とするため、TextureLoader.cpp側の遅延初期化(WIC用、実際に
    // テクスチャを読むまで呼ばれない)より前にここで済ませる。
    const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const ComInitGuard comInitGuard(comInitResult);
    const bool comInitialized = SUCCEEDED(comInitResult) || comInitResult == RPC_E_CHANGED_MODE;

    rs::app::AppCommandLine cli = rs::app::ParseCommandLine(argc, argv);
    if (cli.shouldExit) return cli.exitCode;

    // ログ出力: レベルを色付きで前置（メッセージ本文にモジュールタグ [mesh] 等を残す）。
    // レベルは既定info。常時onの内訳/件数ログはdebugなので、
    // 必要なときだけ以下の優先順で引き上げる: --log-level > 環境変数RS_LOG_LEVEL >
    // RS_PROFILE_LOAD/RS_PROFILE_FRAMEのいずれか設定時はdebug > 既定info。
    spdlog::set_pattern("[%^%l%$] %v");
    {
        std::string levelStr = cli.logLevel;
        if (levelStr.empty()) {
            if (const char* env = rs::platform::ReadEnvironmentVariable("RS_LOG_LEVEL")) levelStr = env;
        }
        spdlog::level::level_enum effectiveLevel = spdlog::level::info;
        if (levelStr.empty() &&
            (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr || rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME") != nullptr)) {
            effectiveLevel = spdlog::level::debug;
        }
        if (!levelStr.empty()) {
            const spdlog::level::level_enum parsed = spdlog::level::from_str(levelStr);
            // spdlog::level::from_strは不明な文字列をoffへ倒すため、"off"自体の指定と
            // 区別できない場合は既定infoへフォールバックして警告する。
            if (parsed == spdlog::level::off && levelStr != "off") {
                spdlog::warn("[startup] --log-level/RS_LOG_LEVELの値が不正です: '{}' (info を使います)",
                             levelStr);
            } else {
                effectiveLevel = parsed;
            }
        }
        spdlog::set_level(effectiveLevel);
        spdlog::flush_on(effectiveLevel);
    }
#ifdef RS_TUNING_HOT_RELOAD
    // 調整値はログの設定の後(警告を出すため)、描画の初期化の前(起動時に1回だけ読む項目にも届けるため)に読む。
    rs::app::LoadRenderTuningFromSource();
#endif

    if (!comInitialized) {
        spdlog::error("CoInitializeExに失敗しました (hr=0x{:08X})",
                      static_cast<unsigned int>(comInitResult));
    }

    // 診断用: 未捕捉例外の内容をabort()前にログへ残す。
    std::set_terminate([]() {
        spdlog::error("[terminate] std::terminate呼び出し");
        try {
            std::exception_ptr eptr = std::current_exception();
            if (eptr) std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            spdlog::error("[terminate] what(): {}", e.what());
        } catch (...) {
            spdlog::error("[terminate] 非std::exception型の例外");
        }
        spdlog::default_logger()->flush();
        std::abort();
    });

    std::string openPluginPath = cli.openPluginPath;
    std::string forceDefFile = cli.forceDefFile;

    // ヘッドレスモード（ウィンドウ/デバイス不要）
    if (!cli.testxPath.empty()) return RunTestX(cli.testxPath);
    if (!cli.testpiPath.empty()) return RunTestPi(cli.testpiPath);
    if (!cli.compatTestRoot.empty()) return RunCompatTest(cli.compatTestRoot);
    if (!cli.testPluginPath.empty()) return RunTestPlugin(cli.testPluginPath, cli.forceDefFile);
    if (!cli.rs2SpikePath.empty()) return RunRs2Spike(cli.rs2SpikePath);
    if (!cli.thumbnailPath.empty()) return rs::app::RunThumbnailCapture(cli.thumbnailPath, cli.capturePngPath);
    if (!cli.layingSetPreviewFolders.empty()) {
        return rs::app::RunLayingSetPreviewCapture(cli.layingSetPreviewFolders, cli.capturePngPath);
    }
    if (!cli.templateThumbnailPath.empty()) {
        return rs::app::RunTemplateThumbnailCapture(cli.templateThumbnailPath, cli.capturePngPath);
    }
    if (cli.tonemapCheck) return rs::app::RunTonemapCheck();

    // プラグインビューワーモード: 位置引数のtxt(関連付け「開く」で渡される)の
    // 親フォルダをプラグインフォルダ、txt自体を定義ファイルとして起動時に読み込む。
    // UIはPI情報+カスタムスイッチのみ(エディタ/列車配置パネルは出さない)。
    const bool viewerMode = !cli.viewerTxtPath.empty();
    if (viewerMode) {
        const std::filesystem::path defPath =
            std::filesystem::absolute(rs::platform_text::Utf8ToWide(cli.viewerTxtPath));
        openPluginPath = rs::platform_text::WideToUtf8(defPath.parent_path().wstring());
        forceDefFile = rs::platform_text::WideToUtf8(defPath.wstring());
    }

    // 初期化/フレームループ/終了処理はAppRuntimeが受け持つ。
    // main()はCLI解析とヘッドレス早期終了分岐だけを持つ。
    rs::app::AppRuntimeGlobals globals{
        g_app, g_camera, g_cabinViewCamera, g_gameClock, g_solarOverrides, g_editor,
        g_editorMeshRevision, g_trackPluginEntryRevision, g_sceneRegistrySceneryRevision,
        g_sceneRegistryStructureRevision,
        g_trackPluginPlacementCache, g_wireEditCache, g_importedWireSpans,
        g_lastArcPreview, g_fontRanges,
    };
    rs::app::AppRuntimeState state(forceDefFile);
    try {
        // 失敗の理由のダイアログは InitializeAppRuntime の中で出す。
        if (!rs::app::InitializeAppRuntime(globals, cli, viewerMode, openPluginPath, state))
            return 1;
        rs::app::RunFrameLoop(globals, state);
        rs::app::ShutdownAppRuntime(globals, state);
        // 起動時に指定したレイアウトを読めなかったら、呼び出し側のスクリプトが気づけるよう失敗で返す。
        if (state.deviceLostExit) return 3;
        return state.startupLayoutFailed ? 2 : 0;
    } catch (const std::exception& e) {
        spdlog::critical("[app] runtime aborted: {}", e.what());
        spdlog::default_logger()->flush();
        // 起動の途中(最初のフレームまで)で抜けたときは、何も出さずに消えないよう理由を見せる。
        if (!state.firstFrameRendered && !rs::app::IsUnattendedRun(cli)) {
            rs::app::ShowStartupFailureDialog(state.localization, e.what());
        }
        return 1;
    }
}

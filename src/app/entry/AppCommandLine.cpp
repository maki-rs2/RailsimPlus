//
// AppCommandLine.cpp - AppCommandLine の実装(main.cppから分離)
//
#include "AppCommandLine.h"

#include <windows.h>
#include <shellapi.h>

#include <CLI/CLI.hpp>

#include <cstdio>
#include <vector>

#include "../../platform/text/Encoding.h"

namespace rs::app {

AppCommandLine ParseCommandLine(int argc, char** argv) {
    AppCommandLine result;

    // 日本語パスを正しく扱うため wide コマンドライン引数を使い UTF-8 化する。
    // (char** argv は system ANSI(=CP932) で渡り、UTF-8 前提の処理と食い違うため)
    std::vector<std::string> args; // UTF-8
    {
        int wArgc = 0;
        LPWSTR* wArgv = CommandLineToArgvW(GetCommandLineW(), &wArgc);
        if (wArgv) {
            for (int i = 0; i < wArgc; ++i)
                args.push_back(rs::platform_text::WideToUtf8(wArgv[i]));
            LocalFree(wArgv);
        } else {
            for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
        }
    }

    // CLI11 で解析する。args は上で UTF-8 化済みなので、正順・プログラム名込みの
    // char* 配列に詰めて parse(argc,argv) へ渡す（日本語パスの扱いを維持）。
    // 未知引数は黙って無視する（allow_extras）。
    CLI::App app{"Railsim Plus"};
    app.allow_extras();
    app.add_option("--smoke", result.smokeFrames, "指定フレーム数だけ描画して終了(ヘッドレススモーク)");
    app.add_option("--testx", result.testxPath, ".x ロードのヘッドレステスト");
    app.add_option("--testpi", result.testpiPath, "PI パースのヘッドレステスト");
    app.add_option("--compat-test", result.compatTestRoot,
                   "ルート配下の全プラグインと全.rs2を一括パースし、互換率と未対応項目を集計して合否を出す");
    app.add_option("--testplugin", result.testPluginPath, "単一プラグインのヘッドレステスト");
    app.add_option("--thumbnail", result.thumbnailPath,
                   "プラグインブラウザのサムネイルを1枚撮って終了(--capture-pngのパスへPNG保存、ウィンドウなし)");
    app.add_option("--laying-set-preview", result.layingSetPreviewFolders,
                   "\";\"区切りのプラグインフォルダを敷設プラグインセットとして合成した画像を1枚撮って終了(--capture-pngのパスへPNG保存、ウィンドウなし)");
    app.add_option("--template-thumbnail", result.templateThumbnailPath,
                   "テンプレート(RailwayPluginSet か TrainGroupTemplate の.txt)のカードの画像を1枚撮って終了(--capture-pngのパスへPNG保存、ウィンドウなし)");
    app.add_flag("--tonemap-check", result.tonemapCheck,
                 "トーンマップのHLSLとCPU基準実装の一致をGPUの読み戻しで確かめて終了(ウィンドウなし)");
    app.add_option("--openplugin", result.openPluginPath, "プラグインフォルダを開く");
    app.add_option("--def", result.forceDefFile, "定義ファイルを明示指定(--testplugin と併用)");
    app.add_option("input", result.viewerTxtPath, "PI定義txt(プラグインビューワーモードで開く)");
    app.add_option("--scene-instance-smoke", result.sceneInstanceSmokePath,
                   "指定プラグインフォルダをグリッド状にインスタンス配置し"
                   "描画位置をヘッドレス検証して終了");
    app.add_option("--scene-instance-count", result.sceneInstanceCount,
                   "--scene-instance-smoke で配置するインスタンス数(既定100)");
    app.add_option("--train-instance-smoke", result.trainInstanceSmokePath, "multi-part train instancing smoke folder");
    app.add_option("--train-instance-count", result.trainInstanceCount, "multi-part train instance count");
    app.add_option("--scene-instance-smoke-batch", result.sceneInstanceSmokeBatchSpec,
                   "複数(フォルダ,配置数)ペアを一括インスタンス配置してヘッドレス"
                   "検証して終了。書式\"フォルダ1=配置数1;フォルダ2=配置数2;...\"");
    app.add_option("--scene-residency-smoke", result.sceneResidencySmokePath,
                   "指定フォルダ直下のサブフォルダを異なる常駐スロットへ配置し"
                   "動的SRVアロケータをヘッドレス検証して終了");
    app.add_option("--scene-residency-max-slots", result.sceneResidencySmokeMaxSlots,
                   "--scene-residency-smoke で使う最大サブフォルダ数(既定30)");
    app.add_option("--rs2spike", result.rs2SpikePath,
                   ".rs2 読込の確認用: RailConnector/RailWayを数件ダンプして終了");
    app.add_option("--openlayout", result.openLayoutPath,
                   ".rs2レイアウトを1本読み込んで敷設エディタ/シーンへ反映しGUIで開く");
    app.add_option("--save-layout", result.saveLayoutPath,
                   "スモーク終了時に現在のレイアウトを .rsl として保存する");
    app.add_option("--layout-digest", result.layoutDigestPath,
                   "スモーク終了時に構造レイアウトダイジェストを書き出す");
    app.add_option("--switch", result.switchOverrides,
                   "--openpluginと併用: 読込後にユーザースイッチの値を上書きする。"
                   "\"名前=値\"または\"添字=値\"(添字は定義順の0始まり)。複数指定可");
    app.add_option("--setting", result.settingOverrides,
                   "起動時の描画設定を上書きする(key=value)。複数指定可。設定ファイルには保存しない");
    app.add_option("--switch-flip-smoke", result.switchFlipSmokeFrames,
                   "--openlayoutと併用: N フレーム描画する間にスロット経路のSceneryエンティティの"
                   "スイッチ値を複数回変更し、スイッチ変更→スロット再利用経路をヘッドレスで"
                   "回帰検証して終了");
    app.add_option("--scene-cycle-smoke", result.sceneCycleSmokeSwitches,
                   "--openlayoutと併用: 読込が落ち着いたあとSceneを順にN回切り替え、切替1回ごとの"
                   "呼び出しとSyncの所要時間を出して終了する(複数Sceneのレイアウト用)");
    app.add_option("--capture-png", result.capturePngPath,
                   "--smoke Nと併用: 最終フレームのバックバッファをこのパスへPNG保存して終了"
                   "(静的レンダリング目視確認の委譲用、対話操作の確認は不可)");
    app.add_option("--camera", result.cameraOverride,
                   "カメラを固定する(`yaw,pitch,distance[,tx,ty,tz]`、角度はradian)。"
                   "--capture-pngで寄りの構図を撮るとき用");
    app.add_option("--log-level", result.logLevel,
                   "ログ出力レベル(trace/debug/info/warn/error/critical/off)。"
                   "未指定時は環境変数RS_LOG_LEVEL、それも無ければ既定infoを使う");
    app.add_flag("--rmlui-proto", result.rmluiProto,
                 "RmlUiのウィジェット・配色確認用ギャラリーを表示する");
    app.add_flag("--rmlui-workspace", result.rmluiWorkspace,
                 "固定ワークスペースを表示する(引数なしの既定と同じ。互換のため残している)");
    app.add_flag("--imgui", result.imgui,
                 "RmlUiの代わりに旧ImGuiのUIで起動する(--rmlui-proto/--rmlui-workspaceとは併用できない)");

    std::vector<char*> cargv;
    cargv.reserve(args.size());
    for (std::string& s : args) cargv.push_back(const_cast<char*>(s.c_str()));
    try {
        app.parse(static_cast<int>(cargv.size()), cargv.data());
    } catch (const CLI::ParseError& e) {
        result.shouldExit = true;
        result.exitCode = app.exit(e);
        return result;
    }

    // UIの既定はRmlUi。プラグインビューワー(位置引数のtxt)はビューアーの画面、それ以外はワークスペース。
    // --rmlui-protoのときは確認画面を優先する。
    if (result.imgui && (result.rmluiProto || result.rmluiWorkspace)) {
        std::fprintf(stderr, "--imgui は --rmlui-proto / --rmlui-workspace と同時に指定できません\n");
        result.shouldExit = true;
        result.exitCode = 1;
        return result;
    }
    if (!result.imgui && !result.rmluiProto) {
        result.rmluiViewer = !result.viewerTxtPath.empty();
        result.rmluiWorkspace = !result.rmluiViewer;
    }
    return result;
}

} // namespace rs::app

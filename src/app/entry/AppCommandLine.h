//
// AppCommandLine.h - コマンドライン引数の解析(main.cppから分離)
//
// wide引数のUTF-8化+CLI11によるオプション定義をまとめる。ヘッドレスモード
// (--testx等)の分岐やビューワーモード判定は呼び出し側(main.cpp)が結果を見て行う。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../rmlui/RmlUiDocumentKind.h"

namespace rs::app {

struct AppCommandLine {
    int smokeFrames = -1;
    std::string testxPath;
    std::string testpiPath;
    std::string compatTestRoot;
    std::string testPluginPath;
    // プラグインブラウザのサムネイルを1枚撮るヘッドレス検証(--capture-pngのパスへPNG保存)。
    std::string thumbnailPath;
    // 敷設プラグインセットの合成プレビューを1枚撮るヘッドレス検証。";"区切りのフォルダ(--capture-pngのパスへPNG保存)。
    std::string layingSetPreviewFolders;
    // テンプレート(編成・敷設)のカードの画像を1枚撮るヘッドレス検証(--capture-pngのパスへPNG保存)。
    std::string templateThumbnailPath;
    bool tonemapCheck = false; // --tonemap-check: トーンマップのHLSLとCPU基準実装の一致を確かめて終了
    std::string openPluginPath;
    // --openpluginと併用: 読込後にユーザースイッチの値を上書きする。"名前=値"または"添字=値"(添字は定義順の0始まり)。
    std::vector<std::string> switchOverrides;
    // --setting key=value: 起動時だけ描画設定へ適用し、設定ファイルには保存しない。
    std::vector<std::string> settingOverrides;
    std::string forceDefFile;
    // 位置引数: PI定義txt。エクスプローラーの関連付け「開く」はこの形で渡してくる。
    std::string viewerTxtPath;

    // InstanceBatchManagerへ指定プラグインフォルダの組立メッシュを
    // グリッド状にN個配置し、1フレーム描画してバックバッファを読み戻し、各配置の
    // 画面上位置が背景色でないことを確認するヘッドレス検証(--smokeの「起動して
    // 落ちない」より一段強い、描画位置の正しさの確認)。
    std::string sceneInstanceSmokePath;
    int         sceneInstanceCount = 100;
    std::string trainInstanceSmokePath;
    int trainInstanceCount = 2;

    // 複数(フォルダ,配置数)ペアを一度にInstanceBatchManagerへ流し込む
    // ヘッドレス検証。実レイアウト調査のような
    // 「ユニークモデル数が多い」ケースを実際のプラグインで
    // 人工再現する。書式: "フォルダ1=配置数1;フォルダ2=配置数2;..."
    // (--scene-instance-smoke/--scene-instance-count とは併用しない、こちらが優先)。
    std::string sceneInstanceSmokeBatchSpec;

    // 常駐スロット経路(SceneRenderer/UploadSceneMeshToGpu)の
    // 動的SRVアロケータ検証。指定フォルダ
    // 直下のサブフォルダを最大maxSlots件、異なる常駐スロットへ実際に配置し、固定数
    // の上限を超えて配置できることを確認するヘッドレス検証。
    std::string sceneResidencySmokePath;
    int         sceneResidencySmokeMaxSlots = 30;

    // .rs2 のAddress精度・Point値の意味・
    // 座標系を実データで確認する使い捨てスパイクツール。
    std::string rs2SpikePath;

    // .rs2レイアウトを1本読み込み、
    // 敷設エディタのTrackGraph+SceneRegistryへ反映してGUIで開く(--openpluginの
    // .rs2版)。ウィンドウ表示前に1回だけ適用する。
    std::string openLayoutPath;
    std::string saveLayoutPath;
    std::string layoutDigestPath;

    // スイッチ変更→スロット再利用経路の回帰検証用。--openlayoutと併用し、一定フレーム
    // 経過後にスロット経路(静的インスタンス化されない)Sceneryエンティティのスイッチ値を
    // GUI操作を模して複数回変更しながらNフレーム描画する。この経路(parkedスロットの
    // セッション再利用)はGUIでスイッチを変更したときにしか通らず、通常の--smokeでは
    // 一度も実行されないため、ヘッドレスで踏ませる手段が無いとリグレッションを検出できない。
    int switchFlipSmokeFrames = -1;

    // Scene切替の所要時間の計測用。--openlayoutと併用し、読込が落ち着いたあとSceneを順にN回
    // 切り替えて、1回ごとの切替呼び出しとSyncの時間を出して終了する。
    int sceneCycleSmokeSwitches = -1;

    // 静的レンダリングの目視確認用(--smokeと併用必須)。
    // 最終smokeフレームの描画直後にバックバッファをこのパスへPNG保存して終了する
    // (対話操作は確認できない、静止画のみ)。
    std::string capturePngPath;

    // カメラの上書き指定(`yaw,pitch,distance[,tx,ty,tz]`のカンマ区切り、角度はradian)。
    // 指定すると毎フレーム読込直後のAABBフィットを上書きし続けるため、読込完了の
    // タイミングに関わらず同一構図を再現できる。--capture-pngで寄りの構図を撮る用
    // (既定のAABBフィットは全体が入る引きの構図しか撮れず、窓・灯具のような
    // 小さい部位の見た目差分を機械判定できないため)。
    std::string cameraOverride;

    // ログ出力レベル(trace/debug/info/warn/error/critical/off、spdlog::level::from_str
    // 準拠)。未指定なら空文字のままとし、main.cpp側で環境変数RS_LOG_LEVEL・
    // RS_PROFILE_*の有無を見て既定値を決める。
    std::string logLevel;

    // RmlUiのDX12統合プロトタイプ(日本語ラベル・
    // ボタン・9スライス枠を1画面出す)を有効化する。既定は無効(通常起動・既存の
    // ヘッドレススモークテストに影響を与えない)。--smoke/--capture-pngと併用できる。
    bool rmluiProto = false;

    // 固定ワークスペース(上部バナー・下部バー・左右タブ・ノッチ)を3Dビューポートへ
    // 重ねて表示する。--openplugin/--openと併用でき、背後の3Dビューは通常どおり
    // 描画・カメラ操作できる。--rmlui-protoと同時指定時はこちらを優先する
    // (UsesRmlUi参照)。引数なしの既定でもtrueになる(ParseCommandLineが決める)。
    bool rmluiWorkspace = false;

    // プラグインビューワー(位置引数のtxt)をRmlUiの画面で表示する。引数なしの既定と同じく、
    // --imguiを付けない限りtrueになる(ParseCommandLineが決める。viewerTxtPathが空ならfalse)。
    bool rmluiViewer = false;

    // 旧ImGuiのUIで起動する。--rmlui-proto/--rmlui-workspaceとは併用できない。
    bool imgui = false;

    // --help等でCLI11がプロセス終了を要求した場合にtrueになる。この場合
    // exitCode をそのまま main の戻り値にして即終了すること。
    bool shouldExit = false;
    int exitCode = 0;
};

// argc/argv(system ANSI)をwide経由でUTF-8化し、CLI11で解析する。
AppCommandLine ParseCommandLine(int argc, char** argv);

// 読み込むRmlUi文書の種類。RmlUiを使わない(ImGui)ときはnullopt。複数のフラグが立っていたら、
// ワークスペース、確認画面、ビューアーの順に優先する。文書の種類の決め方はここ1箇所にする。
inline std::optional<rmlui::RmlUiDocumentKind> RmlUiDocumentKindFor(const AppCommandLine& cli) {
    if (cli.rmluiWorkspace) return rmlui::RmlUiDocumentKind::Workspace;
    if (cli.rmluiProto) return rmlui::RmlUiDocumentKind::Proto;
    if (cli.rmluiViewer) return rmlui::RmlUiDocumentKind::Viewer;
    return std::nullopt;
}

// 「RmlUiを使うか」を1箇所にまとめた述語。AppRuntime.cpp/MainWindow/Appに
// cli.rmluiProto判定を散らさず、ここへ寄せる。
inline bool UsesRmlUi(const AppCommandLine& cli) { return RmlUiDocumentKindFor(cli).has_value(); }

// 人が見ていない検証用の起動(スモーク・PNGキャプチャなど)か。モーダルのダイアログや
// ネットワークの確認など、人がいる前提の処理を止める判定はここ1箇所にする。
inline bool IsUnattendedRun(const AppCommandLine& cli) {
    return cli.smokeFrames > 0 || cli.switchFlipSmokeFrames > 0 || cli.sceneCycleSmokeSwitches > 0 ||
        !cli.sceneInstanceSmokePath.empty() || !cli.sceneInstanceSmokeBatchSpec.empty() ||
        !cli.trainInstanceSmokePath.empty() || !cli.sceneResidencySmokePath.empty() ||
        !cli.capturePngPath.empty() || !cli.thumbnailPath.empty();
}

} // namespace rs::app

//
// SceneInstanceSmokeTest.h - --scene-instance-smoke ヘッドレス検証
//
// レンダリング基盤のみのヘッドレス検証。指定プラグインの組立
// メッシュをInstanceBatchManagerへグリッド状にN個配置し、最初のフレームで
// バックバッファを読み戻して背景色以外のピクセルが十分にあることを確認する
// (GUI目視の代わりに「正しい位置に描画されたか」を自動検証する)。main.cppの
// フレームループ本体から起動時セットアップ/フレーム0での判定処理を分離する。
//
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rs::app {

class App;
class CameraController;

// 起動時1回: 指定プラグインフォルダを読み込み、InstanceBatchManagerへ
// count個のインスタンスをグリッド状に配置する。戻り値trueなら
// EvaluateSceneInstanceSmokeCapture をフレーム0の描画後に呼ぶこと。
bool SetupSceneInstanceSmoke(App& app, CameraController& camera,
                             const std::string& folderUtf8, int count);

// 複数(フォルダ,配置数)ペアを一括でInstanceBatchManagerへ流し込む。
// 実レイアウトのように「ユニークモデル数が多い」(橋脚だけで十数種など)ケースを
// 実際のプラグインで人工再現するための検証用。
// 1件でもEnsureBatch失敗があればログに警告するが
// 処理は継続する(他のエントリの検証結果に影響させないため)。戻り値trueなら
// EvaluateSceneInstanceSmokeCapture をフレーム0の描画後に呼ぶこと。
bool SetupSceneInstanceSmokeBatch(App& app, CameraController& camera,
                                  const std::vector<std::pair<std::string, int>>& entries);

// "フォルダ1=配置数1;フォルダ2=配置数2;..." 形式の文字列をパースする
// (--scene-instance-smoke-batch用)。区切り文字の前後空白は無視する。
// 配置数が数値変換できないエントリはスキップする。
std::vector<std::pair<std::string, int>> ParseSceneInstanceSmokeBatchSpec(const std::string& spec);

// フレーム0でRequestBackBufferCapture済みのRender()呼び出し後に呼ぶ。
// 読み戻したバックバッファの非背景色ピクセル比率を判定してPASS/FAILをログし、
// PostQuitMessage(0)でプロセスを終了させる。
void EvaluateSceneInstanceSmokeCapture(App& app);

} // namespace rs::app

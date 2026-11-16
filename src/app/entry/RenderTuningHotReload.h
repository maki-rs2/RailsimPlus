//
// RenderTuningHotReload.h - render_tuning.json の保存を見つけて、調整値を読み直す。
//
// 呼ぶのはDebugビルドの起動時とフレームループだけ(main.cpp・AppRuntime.cpp の RS_TUNING_HOT_RELOAD)。
// この関数群自体はビルド種別によらず使える。
//
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "../../platform/file/StableFileWatcher.h"
#include "../../render/resource/RenderTuning.h"

namespace rs::app {

// 監視器が書き込みの落ち着いた中身を返したら解析して、読めたときだけ値を返す。
// 解析できないとき(JSONの文法の誤り・キーの型違いなど)は、警告を出して nullopt(呼び出し側は前の値のまま続ける)。
// 公開はしない(グローバルの調整値に触らないので、テストできる)。
std::optional<rs::render::RenderTuning> PollRenderTuningReload(rs::platform_file::StableFileWatcher& watcher,
                                                               double nowSeconds);

// ファイルを読んで解析する。読めない・解析できないときは nullopt(解析できないときは警告を出す)。
// 公開はしない(テストできる)。
std::optional<rs::render::RenderTuning> ReadRenderTuningFile(const std::filesystem::path& path);

// ソースツリーの render_tuning.json を読んで公開する(読めなければ埋め込みの値のまま)。
// 起動時に1回だけ読む項目(シェーダの定義値・大気LUTなど)にも新しい値が届くよう、描画の初期化より前に呼ぶ。
void LoadRenderTuningFromSource();

// 監視器を(無ければ)作り、PollRenderTuningReload の結果を RenderTuning::PublishReloaded で公開する。
// メインスレッドのフレーム先頭から1回だけ呼ぶ(フレームの途中で値が変わらないように)。
void TickRenderTuningHotReload(std::optional<rs::platform_file::StableFileWatcher>& watcher, double nowSeconds);

}  // namespace rs::app

//
// ThumbnailCapture.h - --thumbnail FOLDER: プラグインブラウザのサムネイルを1枚撮ってPNGへ保存する
//
// ウィンドウを出さず、専用のD3D12デバイスだけを作って、ブラウザと同じ経路
// (PluginImageWorker → PluginThumbnailRecipe → ThumbnailRenderer)で1枚撮る。
// ワーカースレッドでの初期化・撮影・破棄までを通すので、描画の確認とワーカーの確認を兼ねる。
// 同じ受付を2回続けて出し、2枚が同じ画像になることも確かめる(1枚ごとの後片付けの確認)。
// 成功したら0、失敗は0以外を返す。
//
#pragma once

#include <string>

namespace rs::app {

// pngPathUtf8 が空なら保存はしない(撮影の成否と中身の統計だけを出す)。
int RunThumbnailCapture(const std::string& folderUtf8, const std::string& pngPathUtf8);

// --laying-set-preview "FOLDER1;FOLDER2;...": 敷設プラグインセット(Rail・Tie・Girder・Line)の合成プレビューを1枚撮る。
// 高さ(Rail・Tieの最大高さ)は敷設と同じ PluginSelector・ComputeRailTieHeights で求める。
int RunLayingSetPreviewCapture(const std::string& foldersUtf8, const std::string& pngPathUtf8);

// --template-thumbnail FILE: テンプレート(RailwayPluginSet\*.txt なら敷設、それ以外は編成 TrainGroupTemplate\*.txt)の
// カードの画像を、ブラウザと同じ経路(PluginImageWorker → TemplateThumbnailRecipe)で1枚撮る。
int RunTemplateThumbnailCapture(const std::string& templatePathUtf8, const std::string& pngPathUtf8);

} // namespace rs::app

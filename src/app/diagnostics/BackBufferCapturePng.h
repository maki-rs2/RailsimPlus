//
// BackBufferCapturePng.h - バックバッファをPNGファイルへ保存する(--capture-png)。
//
// GUI自動操作・画像解析はトークン消費が大きく不確実なため原則メインでは行わない
// 方針だが、静的なレンダリング結果の目視確認だけを別の担当へ切り出して
// 委譲できるよう、ヘッドレスにPNGを1枚出力する経路を
// 用意する。--smoke Nと併用し、最終フレーム描画直後に呼ぶ(SceneInstanceSmokeTest
// のフレーム0キャプチャと同じ`App::RequestBackBufferCapture`/`TakeCapturedBackBuffer`
// を使う。対話操作(クリック/ドラッグ)の確認はできない、静止画のみ)。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace rs::app {

// pixels(RGBA8、rowPitch込み)をUTF-8パスへPNGとして保存する。DirectXTexの
// SaveToWICFileを使う(既存の.pngテクスチャ読込と同じWIC経由、追加のリンク設定
// 不要)。成功したらtrue、失敗時はlogへエラーを積んでfalse。
bool SaveBackBufferPng(const std::string& pathUtf8, const std::vector<uint8_t>& pixels,
                       UINT width, UINT height, UINT rowPitch, std::vector<std::string>& log);

} // namespace rs::app

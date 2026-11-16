//
// ScreenshotCapture.h - F12の3Dだけ(UIなし)スクリーンショット
//
// 描画前にRequestIfTriggeredでキー入力を見て読み戻しを要求し、描画後にSaveIfPendingで
// <実行ファイルのフォルダ>/screenshots/ へPNG保存する。撮る時点はトーンマップ後・UI描画前
// (GpuContext::RecordSceneOnlyCaptureIfRequested)。フォトモードとは別の簡易機能。
//
#pragma once

#include "../input/InputSource.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rs::app {

class App;

struct ScreenshotSaveResult {
    std::string pathUtf8;
    std::string errorKey;
};
ScreenshotSaveResult SaveScreenshotPng(const std::vector<std::uint8_t>& pixels,
                                       std::uint32_t width, std::uint32_t height, std::uint32_t rowPitch);

class ScreenshotCapture {
public:
    // F12が押されたフレームなら3Dだけの読み戻しを要求して撮影待ちにする。
    // otherCaptureRequested=--capture-png/スモークが同じフレームで読み戻しを要求済みのときは、
    // 衝突を避けてこのフレームは見送る(キーは押した瞬間しか見ないので撮影されない)。
    // 要求したらtrue(撮影音を鳴らす側が使う)。
    bool RequestIfTriggered(const rs::app::input::InputSource& input, App& app,
                            bool otherCaptureRequested);

    // 撮影待ちなら読み戻した画像をPNGで保存する(結果はspdlogへ)。描画(App::Render)の後に呼ぶ。
    void SaveIfPending(App& app);

private:
    bool pending_ = false;
};

} // namespace rs::app

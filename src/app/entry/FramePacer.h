//
// FramePacer.h - FPS上限のためのフレーム末尾の待機
//
#pragma once

#include <chrono>

#include <windows.h>

namespace rs::app {

class FramePacer {
public:
    FramePacer() = default;
    ~FramePacer();
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    // フレームの最後に呼ぶ。前回の呼び出しから1/fpsLimit秒たつまで待つ。
    // fpsLimitが0以下なら待たず、基準時刻だけ今に合わせる(上限を戻した直後に遅れを取り戻そうとしないため)。
    void WaitForNextFrame(int fpsLimit);

private:
    // 残り時間が長いうちは高精度タイマーで眠り、末尾の短い区間はスピンで待つ。
    void WaitUntil(std::chrono::steady_clock::time_point target);

    std::chrono::steady_clock::time_point nextFrameTime_{};
    HANDLE timer_ = nullptr; // 高精度の待機タイマー。作れない環境ではnullptrのままスピンだけで待つ
    bool timerCreateTried_ = false;
};

} // namespace rs::app

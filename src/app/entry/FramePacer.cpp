#include "FramePacer.h"

#include <thread>

namespace rs::app {
namespace {

using Clock = std::chrono::steady_clock;

// 高精度タイマーの誤差の見込み。この分を残してスピンへ切り替える。
constexpr std::chrono::microseconds kSleepMargin(2000);

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

} // namespace

FramePacer::~FramePacer() {
    if (timer_ != nullptr) ::CloseHandle(timer_);
}

void FramePacer::WaitForNextFrame(int fpsLimit) {
    const Clock::time_point now = Clock::now();
    if (fpsLimit <= 0) {
        nextFrameTime_ = now;
        return;
    }
    const Clock::duration interval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(fpsLimit)));
    Clock::time_point target = nextFrameTime_ + interval;
    // 1フレーム以上遅れているときは遅れを取り戻さず、今を基準にやり直す。
    if (target < now) target = now;
    WaitUntil(target);
    nextFrameTime_ = target;
}

void FramePacer::WaitUntil(Clock::time_point target) {
    if (!timerCreateTried_) {
        timerCreateTried_ = true;
        timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    }
    if (timer_ != nullptr) {
        const Clock::duration sleepFor = target - Clock::now() - kSleepMargin;
        if (sleepFor > Clock::duration::zero()) {
            // 負の値は相対時間(100ns単位)。
            LARGE_INTEGER dueTime;
            dueTime.QuadPart =
                -std::chrono::duration_cast<std::chrono::nanoseconds>(sleepFor).count() / 100;
            if (::SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE)) {
                ::WaitForSingleObject(timer_, INFINITE);
            }
        }
    }
    while (Clock::now() < target) {
        std::this_thread::yield();
    }
}

} // namespace rs::app

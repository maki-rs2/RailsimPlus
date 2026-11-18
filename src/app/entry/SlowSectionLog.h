//
// SlowSectionLog.h - 長く止まった区間を警告ログに出す(フリーズの区間を切り分ける計測用)
//
#pragma once

#include <chrono>

#include <spdlog/spdlog.h>

namespace rs::app {

// スコープを抜けたとき、生成からの経過が閾値(既定100ms)を超えていたら `[slow] 名前 ms` を警告で出す。
class SlowSectionLog {
public:
    explicit SlowSectionLog(const char* name, double thresholdMs = kThresholdMs)
        : name_(name), thresholdMs_(thresholdMs), start_(std::chrono::steady_clock::now()) {}
    ~SlowSectionLog() {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
        if (ms >= thresholdMs_) spdlog::warn("[slow] {} {:.0f}ms", name_, ms);
    }
    SlowSectionLog(const SlowSectionLog&) = delete;
    SlowSectionLog& operator=(const SlowSectionLog&) = delete;

private:
    static constexpr double kThresholdMs = 100.0;
    const char* name_;
    double thresholdMs_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace rs::app

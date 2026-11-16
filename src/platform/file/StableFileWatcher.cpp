#include "StableFileWatcher.h"

#include <spdlog/spdlog.h>

#include <system_error>
#include <utility>

#include "FileRead.h"

namespace rs::platform_file {

StableFileWatcher::StableFileWatcher(std::filesystem::path path, double intervalSeconds)
    : path_(std::move(path)), intervalSeconds_(intervalSeconds) {}

std::optional<StableFileWatcher::Stamp> StableFileWatcher::ReadStamp() const {
    std::error_code timeError;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path_, timeError);
    if (timeError) return std::nullopt;
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path_, sizeError);
    if (sizeError) return std::nullopt;
    return Stamp{writeTime, size};
}

std::optional<std::string> StableFileWatcher::Poll(double nowSeconds) {
    if (hasChecked_ && nowSeconds - lastCheckSeconds_ < intervalSeconds_) return std::nullopt;
    hasChecked_ = true;
    lastCheckSeconds_ = nowSeconds;

    const std::optional<Stamp> stamp = ReadStamp();
    if (!baselineTaken_) {
        baselineTaken_ = true;
        baseline_ = stamp;
        return std::nullopt;
    }
    // 無いとき(保存の途中で差し替え中など)は、現れるまで待つ。基準は変えない。
    if (!stamp) {
        pending_.reset();
        return std::nullopt;
    }
    if (baseline_ && *stamp == *baseline_) {
        pending_.reset();
        return std::nullopt;
    }
    // 基準と違う組を初めて見た(または、見ている間にまた変わった)。次の確認でも同じなら読む。
    if (!pending_ || !(*pending_ == *stamp)) {
        pending_ = stamp;
        return std::nullopt;
    }

    std::optional<std::string> text = ReadFileText(path_);
    if (!text) {
        // 基準を更新しないので、次の確認で読み直す。
        if (!warnedWriteTime_ || *warnedWriteTime_ != stamp->writeTime) {
            warnedWriteTime_ = stamp->writeTime;
            spdlog::warn("[file] 変更を検出したが読めなかった。次の確認で再試行する: {}", path_.string());
        }
        return std::nullopt;
    }
    baseline_ = stamp;
    pending_.reset();
    return text;
}

}  // namespace rs::platform_file

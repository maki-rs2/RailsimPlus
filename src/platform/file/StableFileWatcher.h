//
// StableFileWatcher.h - ファイルの更新を見つけ、書き込みが落ち着いてから中身を返す。
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace rs::platform_file {

// 一定間隔でファイルの「更新時刻とサイズの組」を見て、変わっていたら中身を返す。
// 変化を見つけた確認ではまだ読まず、次の確認でも組が同じだったとき(書き込み中でないとき)にだけ読む。
// 時刻は呼び出し側が渡す(内部で時計を読まない)。メインスレッドなど1つのスレッドから使う前提。
class StableFileWatcher {
public:
    StableFileWatcher(std::filesystem::path path, double intervalSeconds);

    // 確認間隔に達していなければ何もしない。最初の確認は、ファイルの現在の組を基準として記録するだけで読まない。
    // 読んだ中身を返したら、その組が新しい基準になる。読めなかったときは基準を更新せず、次の確認でやり直す。
    std::optional<std::string> Poll(double nowSeconds);

private:
    struct Stamp {
        std::filesystem::file_time_type writeTime;
        std::uintmax_t size = 0;
        bool operator==(const Stamp& other) const { return writeTime == other.writeTime && size == other.size; }
    };

    // 更新時刻かサイズが取れなければ nullopt(ファイルが無い・差し替え中など)。
    std::optional<Stamp> ReadStamp() const;

    std::filesystem::path path_;
    double intervalSeconds_;
    bool hasChecked_ = false;
    double lastCheckSeconds_ = 0.0;
    // 最初の確認を済ませたか(済ませるまでは基準が無い)。
    bool baselineTaken_ = false;
    // 最後に読めた(または起動時に記録した)組。ファイルが無かったときは nullopt。
    std::optional<Stamp> baseline_;
    // 基準と違う組を見つけ、次の確認でも同じか見ている組。
    std::optional<Stamp> pending_;
    // 読めなくて警告を出した更新時刻(同じ更新時刻では警告を繰り返さない)。
    std::optional<std::filesystem::file_time_type> warnedWriteTime_;
};

}  // namespace rs::platform_file

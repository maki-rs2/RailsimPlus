//
// UpdateApplier.h - 差し替えとロールバック。Windows専用。
//
// 実行中の実行ファイルとDLLは、リネームはできる(上書き・削除はできない)。そこで、本名を `.bak` へ
// 退避してから `.new` を本名へ置く。作業の前にマーカーを書くので、どの瞬間に落ちても、
// マーカーの計画から元の状態へ戻せる。
//
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "UpdateMarker.h"
#include "UpdatePlan.h"

namespace rs::app {

struct UpdateApplyOptions {
    // 共有違反(ERROR_SHARING_VIOLATION)でリネームに失敗したときのやり直し。ウイルス対策ソフトが
    // 新しいファイルを開いていることがある。
    int renameRetryCount = 5;
    std::chrono::milliseconds renameRetryInterval{100};
    // 空き容量の取得。空なら QueryFreeDiskBytes。
    std::function<std::optional<std::uint64_t>(const std::filesystem::path&)> queryFreeBytes;
    // 実行ファイルの起動(第1引数=実行ファイル、第2引数=作業フォルダ)。空なら LaunchProcess。
    std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> launchProcess;
    // リネームをやり直す前に呼ぶ。
    std::function<void()> onRenameRetry;
};

struct UpdateApplyRequest {
    std::filesystem::path installDirectory;
    std::filesystem::path stagingDirectory;
    UpdatePlan plan;
    std::string fromVersion;
    std::string toVersion;
};

enum class UpdateApplyStatus {
    Applied,            // マーカーは pending(attempts=0)。新しい実行ファイルの起動は呼び出し側が行う
    InsufficientSpace,  // 空き容量が足りない。何も書いていない
    Failed,             // 失敗した。事前確認で止まったか、途中で戻した(マーカーは rolledBack)
    RollbackIncomplete, // 失敗して戻そうとしたが、戻しきれなかった(マーカーは残る)
};

// 計画どおりに差し替える。失敗の理由は outError(英語の診断文言)。
UpdateApplyStatus ApplyUpdate(const UpdateApplyRequest& request, const UpdateApplyOptions& options,
                              std::string& outError);

// マーカーの計画にある全ファイルを元に戻す。1ファイルが失敗しても残りを続ける。何度呼んでも同じ結果になる。
// 全部戻せたらマーカーを rolledBack にして真を返す。戻しきれなかったときはマーカーをそのままにして偽。
bool RollbackUpdate(const std::filesystem::path& installDirectory, UpdateMarker marker,
                    const UpdateApplyOptions& options);

// 実行ファイルを起動する(options.launchProcess があればそれ、無ければ LaunchProcess)。
bool LaunchExecutable(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory,
                      const UpdateApplyOptions& options);

// 計画にあるファイルの `.bak` `.new` `.failed` を消す。消せないものがあれば偽(失敗扱いにはせず、残るだけ)。
bool CleanupUpdateFiles(const std::filesystem::path& installDirectory, const UpdatePlan& plan);

} // namespace rs::app

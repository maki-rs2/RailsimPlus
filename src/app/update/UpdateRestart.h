//
// UpdateRestart.h - 終了後の差し替えと、新しい実行ファイルの起動。Windows専用。
//
#pragma once

#include <filesystem>
#include <string>

#include "UpdateApplier.h"

namespace rs::app {

struct UpdateRestartRequest {
    std::filesystem::path executablePath; // main の最初に取ったパス
    std::filesystem::path stagingDirectory;
    std::string fromVersion;
    std::string toVersion;
};

// staging から計画を作って差し替え、新しい実行ファイルを起動する。ShutdownAppRuntime の後に呼ぶ。
// 新しい実行ファイルを起動できたら真。差し替えが失敗したときは、戻した状態で偽を返す。
// 差し替えたのに起動できなかったときは、ロールバックして、戻した実行ファイルを起動して偽を返す。
bool ApplyUpdateAndRelaunch(const UpdateRestartRequest& request, const UpdateApplyOptions& options = {});

} // namespace rs::app

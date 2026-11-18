//
// UpdateMarker.h - 差し替えの途中経過を残すマーカー(RailsimPlus.update.json)と、起動時の判定。
//
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "UpdatePlan.h"

namespace rs::app {

inline constexpr int kUpdateMarkerFormatVersion = 1;

enum class UpdateMarkerState { Applying, Pending, RolledBack };

struct UpdateMarker {
    int formatVersion = kUpdateMarkerFormatVersion;
    std::string fromVersion;
    std::string toVersion;
    // 操作の一覧だけを保存する(directoriesToCreate は保存しない)。
    UpdatePlan plan;
    int attempts = 0; // 起動の試行回数。差し替え直後は0
    UpdateMarkerState state = UpdateMarkerState::Applying;
};

enum class UpdateMarkerReadStatus { Missing, Ok, Corrupt };

struct UpdateMarkerRead {
    UpdateMarkerReadStatus status = UpdateMarkerReadStatus::Missing;
    UpdateMarker marker; // status が Ok のときだけ有効
};

// installDirectory にあるマーカーのパス。
std::filesystem::path UpdateMarkerPath(const std::filesystem::path& installDirectory);

// 無い・読めた・壊れている(JSON不正・必須項目なし・未知の形式の版や状態・安全でないパス)を区別して返す。
UpdateMarkerRead ReadUpdateMarker(const std::filesystem::path& markerPath);

// 原子的に書く。失敗したら outError に英語の診断文言を入れて偽。
bool WriteUpdateMarker(const std::filesystem::path& markerPath, const UpdateMarker& marker, std::string& outError);

enum class StartupAction {
    None,                // マーカーが無い
    CorruptMarker,       // 壊れている
    RollbackAndRelaunch, // 差し替えの途中で落ちた、または新版が最初のフレームまで届かなかった
    NotifyRolledBack,    // 前回ロールバックした
    DiscardStaleMarker,  // 手で差し替えられた
    ConfirmPendingStart, // 新版の最初の起動
};

// 起動時の動作を決める。上から順に判定する。
StartupAction DecideStartupAction(const UpdateMarkerRead& read, std::string_view currentVersion);

} // namespace rs::app

//
// AutoRecovery.h - 自動復元スナップショットの保存先解決・保存判定・異常終了マーカー。
//
// スナップショットは正規のレイアウト
// ファイルを上書きせず、プロファイル領域へ文書GUIDごとに1世代だけ置く。ルートは引数で受け取り
// 関数内で既定値へ解決しない(テストが実環境のプロファイル領域へ書かないようにするため)。
//
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../../world/alignment/Guid.h"
#include "../action/LayoutActionResult.h"

namespace rs::app {

// %LOCALAPPDATA%\RailsimPlus\autorecover。環境変数が引けない場合は相対パスに切り替える。
std::filesystem::path DefaultAutoRecoveryRoot();
// <root>/<GUID>/layout.rsl。
std::filesystem::path AutoRecoverySnapshotPath(const std::filesystem::path& root,
                                               const rs::world::Guid& guid);

// 保存間隔の設定と前回試行時刻。
struct AutoRecoverySchedule {
    bool enabled = true;
    int intervalSeconds = 300;
    double lastAttemptSeconds = 0.0;
};

// 純関数。有効かつ未保存変更があり、編集操作中でなく、間隔が経過したときだけtrue。
bool ShouldWriteAutoRecovery(const AutoRecoverySchedule& schedule, bool dirty,
                             bool editInProgress, double nowSeconds);
// 通常保存に成功したときの整理。文書GUIDのディレクトリごと消す。
bool ClearAutoRecovery(const std::filesystem::path& root, const rs::world::Guid& guid);

struct AutoRecoveryCandidate {
    rs::world::Guid guid;
    std::filesystem::path snapshotPath;
    // スナップショットの元の文書の保存先(無ければ空)と表示名(.rs2のファイル名など。無ければ空)。
    std::string sourcePathUtf8;
    std::string displayNameUtf8;
    std::filesystem::file_time_type writeTime{};
};

// スナップショットの隣に、元の文書の保存先と表示名を置く(画面に出す名前と、復元後の保存先に使う)。
// スナップショット本体は保存と同じ形式のままにするため、別のファイルにする。
LayoutActionResult WriteAutoRecoverySource(const std::filesystem::path& root, const rs::world::Guid& guid,
                             const std::string& sourcePathUtf8, const std::string& displayNameUtf8);

// ルート配下の復元候補を列挙する。ルートが無ければ空。
std::vector<AutoRecoveryCandidate> FindAutoRecoveryCandidates(const std::filesystem::path& root);
// 異常終了マーカー。起動時に置き正常終了で消すため、起動時に残っていれば前回が異常終了。
bool WriteCrashMarker(const std::filesystem::path& root);
bool ClearCrashMarker(const std::filesystem::path& root);
bool HasCrashMarker(const std::filesystem::path& root);

}  // namespace rs::app

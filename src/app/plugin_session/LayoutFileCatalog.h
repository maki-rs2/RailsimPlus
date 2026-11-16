//
// LayoutFileCatalog.h - レイアウトの一覧の画面に並べるファイルを集める。最近使ったファイルの一覧の更新もここ。
//
// RmlUiに依存しない。ファイルの中身は読まない(サムネイルは別スレッドで読む)。
//
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace rs::app {

// 最近使ったファイルの上限。
inline constexpr std::size_t kMaxRecentLayouts = 10;

struct LayoutFileEntry {
    std::string pathUtf8;
    std::string nameUtf8; // 拡張子を除いたファイル名
    std::string dateUtf8; // 更新日時(年/月/日 時:分)
    std::filesystem::file_time_type writeTime{};
};

// directoryの直下の.rslを、新しい順に返す(サブフォルダは見ない)。フォルダが無ければ空。
std::vector<LayoutFileEntry> ListLayoutFolder(const std::filesystem::path& directory);
// 登録したプラグインのルートそれぞれの Layout フォルダ直下の .rs2 を返す。ルートの登録順にまとめ、
// 同じルートの中は名前順(UTF-8のバイト順)にする。Layout フォルダがないルートは飛ばす。
std::vector<LayoutFileEntry> ListRs2Layouts(const std::vector<std::string>& pluginRoots);
// 拡張子が .rs2 か(大文字小文字を区別しない)。
bool IsRs2Path(const std::string& pathUtf8);

// pathsを使った順のまま、ファイルとして存在するものだけ返す。
std::vector<LayoutFileEntry> ListExistingLayouts(const std::vector<std::string>& paths);

// 先頭へ足す。同じファイル(大文字小文字と区切りの違いを除く)があれば前の項目を除き、上限を超えた古い項目を捨てる。
void AddRecentLayout(std::vector<std::string>& recentPaths, const std::string& pathUtf8);
// 存在しない項目を除く。何か除いたらtrue。
bool RemoveMissingRecentLayouts(std::vector<std::string>& recentPaths);

// ファイル名として使えるか(空白だけでなく、Windowsで使えない文字を含まない)。
bool IsValidLayoutFileName(const std::string& nameUtf8);
// パスから拡張子を除いたファイル名を返す。
std::string LayoutNameFromPath(const std::string& pathUtf8);
// ファイルの更新日時を、ローカル時刻の「年/月/日 時:分」にする。
std::string FormatLayoutFileTime(std::filesystem::file_time_type time);

} // namespace rs::app

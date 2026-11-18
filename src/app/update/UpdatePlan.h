//
// UpdatePlan.h - 差し替えの計画(どのファイルを置換・追加・削除するか)を作る純関数。
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rs::app {

// ZIPの中身の一覧。展開した staging と、インストール先の前回の一覧の両方で、この名前で置く。
inline constexpr std::string_view kInstalledFilesName = "installed-files.txt";
// 差し替えの最後に回す実行ファイル。
inline constexpr std::string_view kExecutableRelativePath = "RailsimPlus.exe";
// 更新の途中経過を残すマーカー。実行ファイルの隣に置く。
inline constexpr std::string_view kUpdateMarkerFileName = "RailsimPlus.update.json";

enum class UpdateOperationKind { Replace, Add, Delete };

struct UpdateOperation {
    std::string path; // インストール先からの相対パス(UTF-8、`/` 区切り)
    UpdateOperationKind kind = UpdateOperationKind::Replace;
};

struct UpdatePlan {
    // 実行順。実行ファイルは必ず最後。
    std::vector<UpdateOperation> operations;
    // Add のために作る必要があるフォルダ(相対パス)。浅い順。マーカーには保存しない。
    std::vector<std::string> directoriesToCreate;
};

// インストール先からの相対パスとして安全か(`..`・絶対パス・`\` や `:`・予約名などを含まない)。
// フォルダを指す末尾 `/` は偽。
bool IsSafeRelativePath(std::string_view relativePath);

// 更新機能自身が作るファイル(`.bak` `.new` `.failed` とマーカー)か。計画には入れない。
bool IsUpdateOwnFile(std::string_view relativePath);

// installed-files.txt の内容から、ファイルの相対パス一覧を取り出す。1行1エントリで、ZIPのエントリ名
// (`RailsimPlus/` つき)と接頭辞なしのどちらも読む。フォルダの行・空行・安全でない行は捨てる。
std::vector<std::string> ParseInstalledFilesList(std::string_view text);

// 相対パスのファイルがインストール先に今あるか。
using FileExistsFn = std::function<bool(const std::string& relativePath)>;

// stagedFiles: staging に展開済みのファイル(installed-files.txt を含む)。
// previousInstalledFiles: 前回の installed-files.txt の内容。無ければ空(Delete は作らない)。
// 一覧に無いファイルは操作に入れない。名前の比較は大文字小文字を区別しない(NTFSでは同じファイルのため)。
UpdatePlan BuildUpdatePlan(const std::vector<std::string>& stagedFiles, std::string_view previousInstalledFiles,
                           const FileExistsFn& existsInInstall);

// 空き容量が必要量以上か。
bool HasEnoughDiskSpace(std::uint64_t requiredBytes, std::uint64_t availableBytes);

} // namespace rs::app

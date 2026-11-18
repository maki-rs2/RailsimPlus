//
// DirectoryWritable.h - フォルダに書き込めるかの判定。
//
#pragma once

#include <filesystem>

namespace rs::platform_file {

// フォルダに作業用ファイルを作れるか。ポータブル配置で、実行ファイルの隣へ設定や更新の
// 作業ファイルを置けるかの判定に使う。フォルダが無いときも偽。
bool IsDirectoryWritable(const std::filesystem::path& directory);

}  // namespace rs::platform_file

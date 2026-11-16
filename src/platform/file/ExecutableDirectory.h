//
// ExecutableDirectory.h - 実行ファイルがあるフォルダの場所。
//
#pragma once

#include <filesystem>

namespace rs::platform_file {

// 実行中の実行ファイルがあるフォルダを返す。取れないときはカレントディレクトリを返す。
std::filesystem::path ExecutableDirectory();

}  // namespace rs::platform_file

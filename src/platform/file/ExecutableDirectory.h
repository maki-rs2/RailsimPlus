//
// ExecutableDirectory.h - 実行ファイルがあるフォルダの場所。
//
#pragma once

#include <filesystem>

namespace rs::platform_file {

// 実行中の実行ファイルのフルパス。取れないときは空。
// 実行ファイルを差し替えるときは、リネームの前に1回だけ取って持ち回る。
std::filesystem::path ExecutablePath();

// 実行中の実行ファイルがあるフォルダを返す。取れないときはカレントディレクトリを返す。
std::filesystem::path ExecutableDirectory();

}  // namespace rs::platform_file

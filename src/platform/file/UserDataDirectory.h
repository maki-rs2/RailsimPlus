//
// UserDataDirectory.h - ユーザーごとのアプリ用データフォルダ(%LOCALAPPDATA%\RailsimPlus)の場所。
//
#pragma once

#include <filesystem>

namespace rs::platform_file {

// %LOCALAPPDATA%\RailsimPlus を返す。環境変数が引けないときは相対パス RailsimPlus を返す。
// フォルダは作らない(使う側が必要なときに作る)。
std::filesystem::path UserDataDirectory();

}  // namespace rs::platform_file

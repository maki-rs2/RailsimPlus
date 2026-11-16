//
// AssetPath.h - ソースツリー相対のアセットパスを存在するファイルのパスへ解決する。
//
#pragma once

#include <string>
#include <string_view>

namespace rs::platform_file {

// relativePath(例 "assets/fonts/xxx.ttf")を、実行ファイルの隣(配布版)、ビルド時の
// プロジェクトルート基準の絶対パス(RS_PROJECT_SOURCE_DIR)、実行ディレクトリ相対の順に試し、
// 最初に読めたものを返す。どれも読めなければ空文字列を返す(呼び出し側でフォールバック処理すること)。
std::string ResolveAssetPath(std::string_view relativePath);

}  // namespace rs::platform_file

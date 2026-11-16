//
// AtomicFileWrite.h - 同一ディレクトリの一時ファイル経由で原子的にファイルを置き換える。
//
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rs::platform_file {

// contentを一時ファイルへ書き切ってからpathへ置き換える。書き込み途中で異常終了しても、
// pathの既存内容は失われない(置換に成功するまで元ファイルへ触れないため)。
// 失敗時はoutErrorへ英語の診断文言を入れ、一時ファイルを削除してfalseを返す。
bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content,
                         std::string& outError);

}  // namespace rs::platform_file

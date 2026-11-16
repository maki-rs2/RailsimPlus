//
// RslThumbnailRead.h - .rslのinfo.thumbnailだけを読む。
//
// 一覧の画面で全ファイルについて呼ぶので、JSON全体を組み立てず、キーを順に読んでinfoの終わりで止める。
//
#pragma once

#include <filesystem>
#include <string>

namespace rs::app {

// info.thumbnailの文字列(Base64のJPEG)を返す。読めない、または撮っていないファイルなら空。
std::string ReadRslThumbnailBase64(const std::filesystem::path& path);

} // namespace rs::app

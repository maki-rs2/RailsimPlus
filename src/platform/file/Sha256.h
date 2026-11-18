//
// Sha256.h - ストリームを読みながらSHA-256を出す(Windows専用。BCrypt)。
//
#pragma once

#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace rs::platform_file {

// 終端まで読んだストリームのSHA-256を、小文字16進の64文字で返す。読み込みに失敗したらnullopt。
// 全体をメモリに載せず、一定サイズずつ読む。
std::optional<std::string> Sha256HexOfStream(std::istream& stream);

// ファイルの中身のSHA-256を、小文字16進の64文字で返す。開けない・読めないときはnullopt。
std::optional<std::string> Sha256HexOfFile(const std::filesystem::path& path);

}  // namespace rs::platform_file

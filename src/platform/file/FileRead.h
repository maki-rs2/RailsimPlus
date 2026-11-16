//
// FileRead.h - ファイル全体をバイナリで読み込む。
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rs::platform_file {

// pathをバイナリモードで開き、全体を文字列として返す。開けなければstd::nullopt。
// UTF-8のBOMは取り除かない(呼び出し側で扱う)。
std::optional<std::string> ReadFileText(const std::filesystem::path& path);

// pathをバイナリモードで開き、全体をバイト列として返す。開けない・読めなければstd::nullopt。
std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path);

}  // namespace rs::platform_file

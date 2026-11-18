//
// DiskSpace.h - フォルダのあるドライブの空き容量。Windows専用。
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace rs::platform_file {

// directory のあるドライブで、呼び出したユーザーが使える空き容量(バイト)。取れないときは空。
std::optional<std::uint64_t> QueryFreeDiskBytes(const std::filesystem::path& directory);

}  // namespace rs::platform_file

#include "DiskSpace.h"

#include <windows.h>

namespace rs::platform_file {

std::optional<std::uint64_t> QueryFreeDiskBytes(const std::filesystem::path& directory) {
    ULARGE_INTEGER freeBytesForUser{};
    if (!::GetDiskFreeSpaceExW(directory.c_str(), &freeBytesForUser, nullptr, nullptr)) return std::nullopt;
    return freeBytesForUser.QuadPart;
}

}  // namespace rs::platform_file

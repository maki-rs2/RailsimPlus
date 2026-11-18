#include "DirectoryWritable.h"

#include <fstream>

namespace rs::platform_file {

bool IsDirectoryWritable(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return false;
    const std::filesystem::path probe = directory / L".write_probe";
    {
        std::ofstream stream(probe, std::ios::binary);
        if (!stream.is_open()) return false;
    }
    std::filesystem::remove(probe, error);
    return true;
}

}  // namespace rs::platform_file

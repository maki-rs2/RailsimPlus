#include "AssetPath.h"

#include <filesystem>
#include <fstream>

#include "ExecutableDirectory.h"

#ifndef RS_PROJECT_SOURCE_DIR
#define RS_PROJECT_SOURCE_DIR "."
#endif

namespace rs::platform_file {

namespace {

bool IsReadable(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

}  // namespace

std::string ResolveAssetPath(std::string_view relativePath) {
    const std::filesystem::path packaged = ExecutableDirectory() / std::filesystem::path(relativePath);
    if (IsReadable(packaged.string())) return packaged.string();
    const std::string primary = std::string(RS_PROJECT_SOURCE_DIR) + "/" + std::string(relativePath);
    if (IsReadable(primary)) return primary;
    const std::string fallback(relativePath);
    if (IsReadable(fallback)) return fallback;
    return std::string();
}

}  // namespace rs::platform_file

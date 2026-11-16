#include "FileRead.h"

#include <fstream>
#include <iterator>

namespace rs::platform_file {

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) return std::nullopt;
    const std::streamoff size = stream.tellg();
    if (size < 0) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!stream) return std::nullopt;
    return bytes;
}

}  // namespace rs::platform_file

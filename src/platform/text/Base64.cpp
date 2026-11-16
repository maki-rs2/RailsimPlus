#include "Base64.h"

namespace rs::platform_text {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int DecodeCharacter(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

} // namespace

std::string EncodeBase64(const std::uint8_t* data, std::size_t size) {
    std::string text;
    text.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::size_t remaining = size - i;
        const std::uint32_t group = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (remaining > 1 ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0u) |
                                    (remaining > 2 ? static_cast<std::uint32_t>(data[i + 2]) : 0u);
        text.push_back(kAlphabet[(group >> 18) & 63u]);
        text.push_back(kAlphabet[(group >> 12) & 63u]);
        text.push_back(remaining > 1 ? kAlphabet[(group >> 6) & 63u] : '=');
        text.push_back(remaining > 2 ? kAlphabet[group & 63u] : '=');
    }
    return text;
}

std::vector<std::uint8_t> DecodeBase64(std::string_view text) {
    if (text.size() % 4 != 0) return {};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool lastGroup = i + 4 == text.size();
        const int padding = lastGroup ? (text[i + 3] == '=') + (text[i + 2] == '=' && text[i + 3] == '=') : 0;
        std::uint32_t group = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const bool isPadding = j >= 4 - static_cast<std::size_t>(padding);
            const int value = isPadding ? 0 : DecodeCharacter(text[i + j]);
            if (value < 0) return {};
            group = (group << 6) | static_cast<std::uint32_t>(value);
        }
        bytes.push_back(static_cast<std::uint8_t>(group >> 16));
        if (padding < 2) bytes.push_back(static_cast<std::uint8_t>(group >> 8));
        if (padding < 1) bytes.push_back(static_cast<std::uint8_t>(group));
    }
    return bytes;
}

} // namespace rs::platform_text

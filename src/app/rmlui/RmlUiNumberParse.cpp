#include "RmlUiNumberParse.h"

#include <charconv>
#include <cmath>
#include <cstddef>

namespace rs::app::rmlui {

std::string_view TrimSpaces(std::string_view text) {
    const char* const kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string_view::npos) return std::string_view();
    const std::size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

namespace {
template <typename T>
bool ParseRealText(const std::string& text, T& value) {
    const std::string_view trimmed = TrimSpaces(text);
    if (trimmed.empty()) return false;
    const char* const end = trimmed.data() + trimmed.size();
    T parsed = 0;
    const std::from_chars_result result = std::from_chars(trimmed.data(), end, parsed);
    if (result.ec != std::errc() || result.ptr != end || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}
} // namespace

bool ParseFloatText(const std::string& text, float& value) { return ParseRealText(text, value); }
bool ParseDoubleText(const std::string& text, double& value) { return ParseRealText(text, value); }

bool ParseIntText(const std::string& text, int& value) {
    const std::string_view trimmed = TrimSpaces(text);
    if (trimmed.empty()) return false;
    const char* const end = trimmed.data() + trimmed.size();
    int parsed = 0;
    const std::from_chars_result result = std::from_chars(trimmed.data(), end, parsed);
    if (result.ec != std::errc() || result.ptr != end) return false;
    value = parsed;
    return true;
}

} // namespace rs::app::rmlui

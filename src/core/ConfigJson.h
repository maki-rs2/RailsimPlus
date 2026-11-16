#pragma once

#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rs::core {

// 読み取り途中の値を公開しない。型違いも構文エラーと同じ失敗として扱う。
template <class T, class Read>
std::optional<T> TryReadConfigJson(std::string_view text, const char* label, Read read) {
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        if (!root.is_object()) throw std::invalid_argument("設定のトップはオブジェクトである必要があります");
        return read(root);
    } catch (const std::exception& error) {
        spdlog::warn("{}: 設定の読込に失敗 ({})", label, error.what());
        return std::nullopt;
    }
}

template <class T, class Read>
T ReadConfigJson(std::string_view text, const char* label, Read read) {
    const std::optional<T> parsed = TryReadConfigJson<T>(text, label, read);
    if (parsed) return *parsed;
    spdlog::warn("{}: 既定値を使います", label);
    return T{};
}

template <class T, class Read>
T ReadEmbeddedConfig(const unsigned char* data, std::size_t size, const char* label, Read read) {
    return ReadConfigJson<T>(std::string_view(reinterpret_cast<const char*>(data), size), label, read);
}

} // namespace rs::core

//
// Base64.h - バイト列とBase64文字列の変換(.rslへ画像を文字列として埋め込むために使う)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rs::platform_text {

std::string EncodeBase64(const std::uint8_t* data, std::size_t size);
// Base64として不正な文字列なら空を返す。
std::vector<std::uint8_t> DecodeBase64(std::string_view text);

} // namespace rs::platform_text

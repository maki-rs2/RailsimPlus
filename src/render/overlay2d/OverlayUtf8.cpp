//
// OverlayUtf8.cpp - 注記の文字列(UTF-8)をコードポイントへ分ける（OverlayGeometryBuilder.cpp から分離）
//
#include "OverlayGeometryBuilder.h"

namespace rs::render {
namespace {

// UTF-8として解釈できないバイト列の代替コードポイント。
constexpr uint32_t kReplacementCodePoint = 0xFFFDU;

} // namespace

std::vector<uint32_t> OverlayGeometryBuilder::DecodeUtf8(std::string_view utf8) {
    std::vector<uint32_t> result;
    for (size_t index = 0; index < utf8.size();) {
        const uint8_t first = static_cast<uint8_t>(utf8[index++]);
        uint32_t value = 0;
        uint32_t remaining = 0;
        uint32_t minimum = 0;
        if (first < 0x80U) {
            result.push_back(first);
            continue;
        }
        if ((first & 0xE0U) == 0xC0U) {
            value = first & 0x1FU;
            remaining = 1;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            value = first & 0x0FU;
            remaining = 2;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            value = first & 0x07U;
            remaining = 3;
            minimum = 0x10000U;
        } else {
            // 先頭バイトとして不正。1バイト読み捨てて置換文字にする。
            result.push_back(kReplacementCodePoint);
            continue;
        }
        if (index + remaining > utf8.size()) {
            result.push_back(kReplacementCodePoint);
            break;
        }
        bool valid = true;
        for (uint32_t count = 0; count < remaining; ++count) {
            const uint8_t next = static_cast<uint8_t>(utf8[index++]);
            if ((next & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            value = (value << 6U) | (next & 0x3FU);
        }
        // 冗長符号化・サロゲート・範囲外は全て置換文字へ落とす。
        const bool surrogate = value >= 0xD800U && value <= 0xDFFFU;
        if (!valid || value < minimum || value > 0x10FFFFU || surrogate) {
            result.push_back(kReplacementCodePoint);
            continue;
        }
        result.push_back(value);
    }
    return result;
}

} // namespace rs::render

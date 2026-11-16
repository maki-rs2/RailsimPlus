#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rs::app::workspace {
enum class CameraScreenParameter { FocalLength, Aperture, ShutterSpeed, Iso, FocusDistance, Count };
inline constexpr std::size_t kCameraScreenParameterCount = static_cast<std::size_t>(CameraScreenParameter::Count);
// 段の先頭に置く「自動」の値。欄の値(text_)はこの文字列のままで、7セグには決まった値の数字を出す。
inline constexpr std::string_view kCameraScreenAutoText = "AUTO";
// 自動のときに7セグへ出す数字の、決まった値がまだ無い間の表示(ISOの基準値)。
inline constexpr std::string_view kCameraScreenAutoFallbackText = "100";
// 入力された文字列が「AUTO」か(大文字小文字を区別しない)。
inline bool IsCameraScreenAutoText(std::string_view text) {
    if (text.size() != kCameraScreenAutoText.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char upper = text[i] >= 'a' && text[i] <= 'z' ? static_cast<char>(text[i] - 'a' + 'A') : text[i];
        if (upper != kCameraScreenAutoText[i]) return false;
    }
    return true;
}
struct CameraScreenSteps {
    std::array<std::vector<std::string>, kCameraScreenParameterCount> values_{{
        {"14", "18", "24", "28", "35", "50", "70", "85", "100", "135", "200", "300"},
        {"1.4", "2", "2.8", "4", "5.6", "8", "11", "16", "22"},
        {"1/8000", "1/4000", "1/2000", "1/1000", "1/500", "1/250", "1/125", "1/60", "1/30", "1/15", "1/8", "1/4", "1/2", "1", "2", "4", "8", "15", "30"},
        {"AUTO", "50", "100", "200", "400", "800", "1600", "3200", "6400", "12800"},
        {"0.1", "0.2", "0.3", "0.5", "1", "2", "3", "5", "10", "20", "50", "100", "1000"}
    }};
    static const CameraScreenSteps& Embedded();
};
}

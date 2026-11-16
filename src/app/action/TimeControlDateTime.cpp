#include "TimeControlDateTime.h"

#include <array>
#include <charconv>
#include <cstddef>

namespace rs::app::time_control_action {
namespace {

// 年の入力範囲(表示が4桁の年になる範囲)。
constexpr int kMinYear = 1;
constexpr int kMaxYear = 9999;
constexpr int kMaxHour = 23;
constexpr int kMaxMinute = 59;

std::string_view Trim(std::string_view text) {
    constexpr std::string_view kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

// separatorで区切った、ちょうどN個の整数に分ける。
template <std::size_t N>
bool SplitIntegers(std::string_view text, char separator, std::array<int, N>& values) {
    for (std::size_t index = 0; index < N; ++index) {
        const bool last = index + 1 == N;
        const std::size_t end = last ? std::string_view::npos : text.find(separator);
        if (!last && end == std::string_view::npos) return false;
        const std::string_view part = Trim(text.substr(0, end));
        const char* const partEnd = part.data() + part.size();
        const std::from_chars_result result = std::from_chars(part.data(), partEnd, values[index]);
        if (part.empty() || result.ec != std::errc() || result.ptr != partEnd) return false;
        if (!last) text.remove_prefix(end + 1);
    }
    return true;
}

} // namespace

DateTimeParseResult ParseDateTimeInput(std::string_view date, std::string_view time) {
    std::array<int, 3> ymd{};
    if (!SplitIntegers(date, '/', ymd)) return {std::nullopt, "日付は「年/月/日」の形で入力してください"};
    std::array<int, 2> hm{};
    if (!SplitIntegers(time, ':', hm)) return {std::nullopt, "時刻は「時:分」の形で入力してください"};
    if (ymd[0] < kMinYear || ymd[0] > kMaxYear) return {std::nullopt, "年は1〜9999で入力してください"};
    const rs::world::DateParts parts{ymd[0], ymd[1], ymd[2]};
    // 月や日が範囲外のときは、通日に直して戻すと別の日付になる。
    const rs::world::DateParts roundTrip = rs::world::ToDateParts(rs::world::FromDateParts(parts));
    if (roundTrip.year != parts.year || roundTrip.month != parts.month || roundTrip.day != parts.day) {
        return {std::nullopt, "存在しない日付です"};
    }
    if (hm[0] < 0 || hm[0] > kMaxHour || hm[1] < 0 || hm[1] > kMaxMinute) {
        return {std::nullopt, "時刻は0:00〜23:59で入力してください"};
    }
    return {DateTimeInput{parts, hm[0], hm[1]}, {}};
}

} // namespace rs::app::time_control_action

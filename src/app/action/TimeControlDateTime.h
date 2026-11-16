//
// TimeControlDateTime.h - 時間操作の窓に入力された日時の文字を読む純粋関数
//
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "../../world/time/CalendarUtil.h"

namespace rs::app::time_control_action {

struct DateTimeInput {
    rs::world::DateParts date;
    int hour = 0;
    int minute = 0;
};

// 読めた日時か、読めない理由(画面に出す日本語の文)。
struct DateTimeParseResult {
    std::optional<DateTimeInput> value;
    std::string error;
};

// date は「年/月/日」、time は「時:分」(どれも整数。前後の空白は無視する)。
// 存在しない日付(2月30日など)と、範囲外の年・時・分は読めない扱いにする。
DateTimeParseResult ParseDateTimeInput(std::string_view date, std::string_view time);

} // namespace rs::app::time_control_action

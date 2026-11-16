#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "../../world/alignment/PlatformRoutine.h"

namespace rs::app::workspace {

// 運行情報の書換の項目。keyは RunInfoChange::Field の番号で、画面の候補と操作の両方がこの表を引く。
struct DiaRunInfoFieldName {
    const char* label;
    int key;
};
inline constexpr std::array<DiaRunInfoFieldName, 5> kDiaRunInfoFieldNames = {{
    {"種別", static_cast<int>(rs::world::RunInfoChange::Field::Type)},
    {"行先", static_cast<int>(rs::world::RunInfoChange::Field::Destination)},
    {"路線名", static_cast<int>(rs::world::RunInfoChange::Field::LineName)},
    {"列番", static_cast<int>(rs::world::RunInfoChange::Field::TrainNumber)},
    {"運用番号", static_cast<int>(rs::world::RunInfoChange::Field::DutyNumber)},
}};

std::string DiaRunInfoFieldLabel(rs::world::RunInfoChange::Field field);

// 画面の動作の名前('stop' / 'reverse' / 'pass')と、動作の日本語の表示名。
const char* DiaActionKey(rs::world::DiaAction action);
const char* DiaActionLabel(rs::world::DiaAction action);
std::optional<rs::world::DiaAction> DiaActionFromKey(std::string_view key);

// 発車時刻の表示。秒が0なら「時:分」、そうでなければ「時:分:秒」。
std::string FormatDiaTime(const rs::world::DiaTimeOfDay& time);
// 「時:分」か「時:分:秒」を読む。分・秒は0〜59、時は0以上。読めなければ false。
bool ParseDiaTime(std::string_view text, rs::world::DiaTimeOfDay& out);

// 停車秒数の表示(整数なら小数点なし)と読み取り(0以上の数)。
std::string FormatDiaSeconds(double seconds);
bool ParseDiaSeconds(std::string_view text, double& out);

} // namespace rs::app::workspace

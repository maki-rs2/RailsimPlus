//
// CalendarUtil.h - 通日番号(dayNumber)⇔年月日・曜日の変換を行う純関数群。
//
// dayNumber は1970-01-01(木曜)を0とする通日番号(グレゴリオ暦を過去/未来へ
// 外挿した"proleptic Gregorian calendar")。GameClock/Rs2Timeの内部表現と
// 揃えるための基準点であり、状態を一切持たない(GUI・プラグイン読込非依存)。
//
#pragma once

#include <cstdint>

namespace rs::world {

// 年月日(グレゴリオ暦)。
struct DateParts {
    int year  = 1970;
    int month = 1;  // 1-12
    int day   = 1;  // 1-31
};

// dayNumber の基準点: 1970-01-01 は木曜日(曜日値4、0=日曜〜6=土曜)。
inline constexpr int64_t kEpochYear    = 1970;
inline constexpr int64_t kEpochMonth   = 1;
inline constexpr int64_t kEpochDay     = 1;
inline constexpr int     kEpochWeekday = 4;  // 木曜

// 西暦 year がグレゴリオ暦のうるう年かどうか。
bool IsLeapYear(int64_t year);

// dayNumber(1970-01-01=0)を年月日へ変換する。
// Howard Hinnant の civil_from_days アルゴリズムに基づく(1970年より前の
// 負の dayNumber にも対応するproleptic Gregorian変換)。
DateParts ToDateParts(int64_t dayNumber);

// 年月日から dayNumber(1970-01-01=0)を求める(ToDateParts の逆変換)。
// .rs2 読込時の初期日時設定・EnvironmentPresets の初期日付設定に使う。
int64_t FromDateParts(const DateParts& parts);

// dayNumber に対応する曜日(0=日曜〜6=土曜)を返す。
int DayOfWeek(int64_t dayNumber);

// dayNumber に対応する年内通日(1月1日=1)を返す。太陽位置計算(季節変化)に使う。
int DayOfYear(int64_t dayNumber);

} // namespace rs::world

//
// GameClock.h - ゲーム内時刻(通日番号+日内経過秒)の進行管理。
//
// 内部表現は (dayNumber_: 1970-01-01基準の通日番号, secondsOfDay_: [0,86400)
// の日内経過秒) のペア。.rs2 Timeブロックの SumDays+Time に直接対応させる
// ための構成であり、長時間実行時の精度劣化も避けられる。
// 年月日・曜日への変換は一切自前で持たず CalendarUtil に委譲する。
// GUI・プラグイン読込非依存の純データ+ロジック(rs::world にのみ依存)。
//
#pragma once

#include <algorithm>

#include "CalendarUtil.h"

namespace rs::world {

class GameClock {
public:
    // 時間倍率の入力範囲。一時停止の0倍から、編集画面で指定できる3000倍まで。
    static constexpr double kMinTimeScale = 0.0;
    static constexpr double kMaxTimeScale = 3000.0;
    static double ClampTimeScale(double scale) {
        return std::clamp(scale, kMinTimeScale, kMaxTimeScale);
    }

    // dtSeconds(ゲーム内経過秒。TimeScaleは呼び出し側が適用済みであること)分、
    // 日内時刻を進める。86400秒を超えた/下回った分は dayNumber_ の繰り上げ/
    // 繰り下げで正規化する。GameClock自身はdtへtimeScale_を掛けない
    // (固定tick方式ではアキュムレータ側で実時間にtimeScale_を掛けて消費する
    // tick数を決めるため、二重に加速がかかるのを防ぐ。TimeScale()は呼び出し側が
    // 「実時間→ゲーム内時間」の換算に使うための保持値)。
    void Tick(double dtSeconds);

    void SetTimeScale(double timeScale) { timeScale_ = timeScale; }
    double TimeScale() const { return timeScale_; }

    // .rs2読込・時刻ワープ用。secondsOfDay は [0,86400) へ正規化して格納する。
    void SetDateTime(int64_t dayNumber, double secondsOfDay);

    int64_t DayNumber() const { return dayNumber_; }
    double SecondsOfDay() const { return secondsOfDay_; }

    // CalendarUtil への薄い委譲(GameClock自体は日付変換ロジックを持たない)。
    int Year() const { return ToDateParts(dayNumber_).year; }
    int Month() const { return ToDateParts(dayNumber_).month; }
    int Day() const { return ToDateParts(dayNumber_).day; }
    int DayOfWeek() const { return rs::world::DayOfWeek(dayNumber_); }

    int Hour() const { return static_cast<int>(secondsOfDay_ / 3600.0); }
    int Minute() const { return static_cast<int>(secondsOfDay_ / 60.0) % 60; }
    int Second() const { return static_cast<int>(secondsOfDay_) % 60; }

    // 日跨ぎを含めた単一の数直線としての絶対ゲーム内秒(dayNumber*86400 + secondsOfDay)。
    // ダイヤの発車時刻判定は「到着からの残り秒のカウントダウン」ではなく、この絶対秒
    // どうしの比較で行う。カウントダウン方式
    // だとTimeScaleやtick粒度で停車時間が変わってしまうため。
    // doubleの仮数53bitに対し現代の日付でも1.8e9秒程度なので、分解能はマイクロ秒未満。
    double AbsoluteSeconds() const { return static_cast<double>(dayNumber_) * 86400.0 + secondsOfDay_; }

private:
    int64_t dayNumber_ = 0;      // 1970-01-01 = 0
    double  secondsOfDay_ = 0.0; // [0, 86400)
    double  timeScale_ = 1.0;
};

// 日内時刻(hour,minute,second)を、absoluteNowSeconds以降で最初に来る絶対ゲーム内秒へ
// 変換する(GameClock::AbsoluteSeconds と同じ数直線上の値を返す)。既に過ぎている
// 時刻は翌日へ送る。hourは24以上や負値でも日内へ正規化する(.rs2のTime=h,m,s は
// 25:30 のような表記を採りうるため)。
//
// GameClockの状態に依存しない純関数なのでクラスには入れず自由関数にしている。
double NextAbsoluteSecondsAt(double absoluteNowSeconds, int hour, int minute, int second);

} // namespace rs::world

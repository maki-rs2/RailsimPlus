//
// InitSteps.h - 初期化の手順を「必須」と「任意」に分けて表で持ち、順に実行する
//
// 必須(デバイス・スワップチェーン・主パスなど)の失敗は起動の失敗にする。
// 任意(影・後処理などの付加的なパス)の失敗は警告ログを出してそのパスを無効にし、起動は続ける。
// どれが必須かは手順の表(InitStep の配列)の importance だけが決める。
//
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace rs::app {

enum class InitImportance { Required, Optional };

struct InitStep {
    const char* name = "";
    InitImportance importance = InitImportance::Required;
    std::function<bool()> run;
};

struct InitStepsResult {
    // 必須の手順がすべて成功したか。
    bool ok = true;
    // 最初に失敗した必須の手順の名前(ok のときは空)。
    std::string failedRequired;
    // 失敗して無効にした任意の手順の名前。
    std::vector<std::string> disabledOptional;

    bool IsDisabled(const std::string& name) const;
};

// 表の順に実行する。必須が失敗したらそこで止め、以降の手順は実行しない。
// 任意が失敗したら警告ログを出して記録し、次の手順へ進む。
InitStepsResult RunInitSteps(std::span<const InitStep> steps);

} // namespace rs::app

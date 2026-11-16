#pragma once

#include <optional>
#include <string>
#include <vector>

#include "GameSettingsPresentation.h"

namespace rs::app::workspace {

struct TimeControlTuning;

struct TimeControlStep {
    std::string label_;
    double scale_ = 0.0;
    bool pause_ = false;
    bool selected_ = false;
    // 早送りの三角の数(一時停止は0)。数によらずアイコンとしての大きさをそろえるため、表示は数ごとに文字の大きさを変える。
    int arrows_ = 0;
};

struct TimeControlPresentation {
    std::vector<TimeControlStep> steps_;
    double scale_ = 0.0;
    std::string scaleLabel_;
    std::string scaleDraft_;
    bool editingScale_ = false;
    bool windowOpen_ = false;
    int year_ = 0;
    int month_ = 0;
    int day_ = 0;
    int hour_ = 0;
    int minute_ = 0;
    int second_ = 0;
    std::string timeText_;
    std::string secondText_;
    std::string dateText_;
    // 日時を変える窓の入力欄(年・月・日・時・分を別の欄にする)。
    std::string yearDraft_;
    std::string monthDraft_;
    std::string dayDraft_;
    std::string hourDraft_;
    std::string minuteDraft_;
    // 自転・公転の選択(名前で持つ)と、並んだボタンの選択肢。確定で日時と一緒に反映する。
    std::string rotationDraft_;
    std::string revolutionDraft_;
    std::vector<GameSettingsChoice> rotations_;
    std::vector<GameSettingsChoice> revolutions_;

    void RefreshDerived();
    void BeginScaleEdit();
    std::optional<double> CommitScale(double value);
    void CancelScaleEdit();
    void OpenWindow();
    // 入力欄の値を、日時の変更に渡す形("年/月/日"・"時:分")にまとめる。
    std::string DateDraft() const;
    std::string TimeDraft() const;
    bool CloseTransient();
    static double ClampScale(double scale);
};

// 一時停止と等速・早送り2段の段の並び(倍率は調整値から)。
std::vector<TimeControlStep> MakeTimeControlSteps(const TimeControlTuning& tuning);
} // namespace rs::app::workspace

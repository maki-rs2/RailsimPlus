#include "TimeControlPresentation.h"

#include <cmath>
#include <format>

#include "TimeControlTuning.h"
#include "../../world/time/GameClock.h"

namespace rs::app::workspace {

std::vector<TimeControlStep> MakeTimeControlSteps(const TimeControlTuning& tuning) {
    return {{"⏸", 0.0, true},
            {"▶", tuning.scales_[0], false, false, 1},
            {"▶▶", tuning.scales_[1], false, false, 2},
            {"▶▶▶", tuning.scales_[2], false, false, 3}};
}

double TimeControlPresentation::ClampScale(double scale) {
    return rs::world::GameClock::ClampTimeScale(scale);
}

void TimeControlPresentation::RefreshDerived() {
    for (TimeControlStep& step : steps_) step.selected_ = step.scale_ == scale_;
    scaleLabel_ = std::format("×{:g}", scale_);
    timeText_ = std::format("{:02}:{:02}", hour_, minute_);
    secondText_ = std::format("{:02}", second_);
    dateText_ = std::format("{:04}/{:02}/{:02}", year_, month_, day_);
}

void TimeControlPresentation::BeginScaleEdit() {
    scaleDraft_ = std::format("{:g}", scale_);
    editingScale_ = true;
}

std::optional<double> TimeControlPresentation::CommitScale(double value) {
    if (!editingScale_) return std::nullopt;
    editingScale_ = false;
    if (!std::isfinite(value)) return std::nullopt;
    value = ClampScale(value);
    scaleDraft_ = std::format("{:g}", value);
    return value;
}

void TimeControlPresentation::CancelScaleEdit() {
    editingScale_ = false;
    scaleDraft_ = std::format("{:g}", scale_);
}

void TimeControlPresentation::OpenWindow() {
    yearDraft_ = std::to_string(year_);
    monthDraft_ = std::to_string(month_);
    dayDraft_ = std::to_string(day_);
    hourDraft_ = std::format("{:02}", hour_);
    minuteDraft_ = std::format("{:02}", minute_);
    windowOpen_ = true;
}

std::string TimeControlPresentation::DateDraft() const {
    return yearDraft_ + "/" + monthDraft_ + "/" + dayDraft_;
}

std::string TimeControlPresentation::TimeDraft() const {
    return hourDraft_ + ":" + minuteDraft_;
}

bool TimeControlPresentation::CloseTransient() {
    if (editingScale_) {
        CancelScaleEdit();
        return true;
    }
    if (!windowOpen_) return false;
    windowOpen_ = false;
    return true;
}
} // namespace rs::app::workspace

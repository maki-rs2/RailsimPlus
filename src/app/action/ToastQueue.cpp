#include "ToastQueue.h"

#include "../../core/ConfigJson.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "EmbeddedToastTuning.h"

namespace rs::app {

namespace {

bool IsValid(const ToastTuning& tuning) {
    return std::isfinite(tuning.infoSeconds) && tuning.infoSeconds > 0.0 &&
           std::isfinite(tuning.warningSeconds) && tuning.warningSeconds > 0.0 &&
           std::isfinite(tuning.errorSeconds) && tuning.errorSeconds > 0.0 && tuning.maxVisible > 0;
}

ToastTuning ReadTuning(const nlohmann::json& root) {
    ToastTuning tuning;
    tuning.infoSeconds = root.value("infoSeconds", tuning.infoSeconds);
    tuning.warningSeconds = root.value("warningSeconds", tuning.warningSeconds);
    tuning.errorSeconds = root.value("errorSeconds", tuning.errorSeconds);
    const int maxVisible = root.value("maxVisible", static_cast<int>(tuning.maxVisible));
    if (maxVisible <= 0) throw std::invalid_argument("maxVisible must be positive");
    tuning.maxVisible = static_cast<std::size_t>(maxVisible);
    if (!IsValid(tuning)) throw std::invalid_argument("toast durations must be positive and finite");
    return tuning;
}

ToastTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<ToastTuning>(
        rs::embedded::kToastTuningJson, rs::embedded::kToastTuningJsonSize, "ToastTuning",
        ReadTuning);
}

}  // namespace

ToastTuning ToastTuning::FromJson(std::string_view json) {
    return ReadTuning(nlohmann::json::parse(json));
}

const ToastTuning& ToastTuning::Embedded() {
    static const ToastTuning tuning = ParseEmbedded();
    return tuning;
}

ToastQueue::ToastQueue(ToastTuning tuning) : tuning_(std::move(tuning)) {
    if (!IsValid(tuning_)) throw std::invalid_argument("toast tuning must be positive and finite");
}

double ToastQueue::Duration(ToastSeverity severity) const {
    switch (severity) {
    case ToastSeverity::Info: return tuning_.infoSeconds;
    case ToastSeverity::Warning: return tuning_.warningSeconds;
    case ToastSeverity::Error: return tuning_.errorSeconds;
    }
    return tuning_.infoSeconds;
}

void ToastQueue::Post(ToastSeverity severity, std::string text) {
    PostWithOptions(severity, std::move(text), {});
}

void ToastQueue::PostWithAction(ToastSeverity severity, std::string text, std::string actionLabel,
                                std::function<void()> onAction) {
    ToastPostOptions options;
    options.actionLabel = std::move(actionLabel);
    options.onAction = std::move(onAction);
    PostWithOptions(severity, std::move(text), std::move(options));
}

void ToastQueue::PostWithOptions(ToastSeverity severity, std::string text, ToastPostOptions options) {
    const std::vector<ActiveToast>::iterator existing =
        std::find_if(activeItems_.begin(), activeItems_.end(), [severity, &text](const ActiveToast& item) {
            return item.item.severity == severity && item.item.text == text;
        });
    if (existing != activeItems_.end()) {
        if (!existing->item.sticky) existing->remainingSeconds = Duration(severity);
        return;
    }

    AddItem(MakeItem(severity, std::move(text), std::move(options)));
}

ToastItem ToastQueue::MakeItem(ToastSeverity severity, std::string text, ToastPostOptions options) const {
    ToastItem item;
    item.severity = severity;
    item.text = std::move(text);
    item.actionLabel = std::move(options.actionLabel);
    item.onAction = std::move(options.onAction);
    item.style = options.style;
    item.sticky = options.sticky || severity == ToastSeverity::Error;
    item.key = std::move(options.key);
    item.closable = options.closable.value_or(item.sticky) || severity == ToastSeverity::Error;
    item.keepOpenOnAction = options.keepOpenOnAction;
    return item;
}

void ToastQueue::AddItem(ToastItem item) {
    const ToastSeverity severity = item.severity;
    ++postedCounts_[static_cast<std::size_t>(severity)];
    activeItems_.push_back({std::move(item), Duration(severity)});
    if (activeItems_.size() > tuning_.maxVisible) activeItems_.erase(activeItems_.begin());
    RefreshItems();
    ++version_;
}

void ToastQueue::UpsertKeyed(ToastSeverity severity, std::string text, ToastPostOptions options) {
    if (options.key.empty()) {
        PostWithOptions(severity, std::move(text), std::move(options));
        return;
    }
    ToastItem item = MakeItem(severity, std::move(text), std::move(options));
    const std::vector<ActiveToast>::iterator existing =
        std::find_if(activeItems_.begin(), activeItems_.end(),
                     [&item](const ActiveToast& active) { return active.item.key == item.key; });
    if (existing == activeItems_.end()) {
        AddItem(std::move(item));
        return;
    }
    const ToastItem& current = existing->item;
    const bool same = current.severity == item.severity && current.text == item.text &&
                      current.actionLabel == item.actionLabel && current.style == item.style &&
                      current.sticky == item.sticky && current.closable == item.closable &&
                      current.keepOpenOnAction == item.keepOpenOnAction;
    if (same) return;
    existing->item = std::move(item);
    if (!existing->item.sticky) existing->remainingSeconds = Duration(severity);
    RefreshItems();
    ++version_;
}

void ToastQueue::DismissKey(std::string_view key) {
    if (key.empty()) return;
    const std::vector<ActiveToast>::iterator existing =
        std::find_if(activeItems_.begin(), activeItems_.end(),
                     [key](const ActiveToast& active) { return active.item.key == key; });
    if (existing == activeItems_.end()) return;
    Dismiss(static_cast<std::size_t>(existing - activeItems_.begin()));
}

void ToastQueue::Dismiss(std::size_t index) {
    if (index >= activeItems_.size()) return;
    activeItems_.erase(activeItems_.begin() + static_cast<std::ptrdiff_t>(index));
    RefreshItems();
    ++version_;
}

void ToastQueue::Advance(double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 || activeItems_.empty()) return;
    const std::size_t previousSize = activeItems_.size();
    for (ActiveToast& item : activeItems_) {
        if (!item.item.sticky) item.remainingSeconds -= deltaSeconds;
    }
    std::erase_if(activeItems_, [](const ActiveToast& item) { return !item.item.sticky && item.remainingSeconds <= 0.0; });
    if (activeItems_.size() != previousSize) {
        RefreshItems();
        ++version_;
    }
}

void ToastQueue::RefreshItems() {
    items_.clear();
    items_.reserve(activeItems_.size());
    for (const ActiveToast& item : activeItems_) items_.push_back(item.item);
}

}  // namespace rs::app

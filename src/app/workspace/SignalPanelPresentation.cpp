#include "SignalPanelPresentation.h"

#include <unordered_map>

namespace rs::app::workspace {

namespace {

// 位置と選択を除いた中身が同じか(作り直しの要否の判定)。
bool SameContent(const SignalPanelSignal& a, const SignalPanelSignal& b) {
    return a.id_ == b.id_ && a.name_ == b.name_ && a.direction_ == b.direction_ &&
           a.aspectText_ == b.aspectText_ && a.forcedRed_ == b.forcedRed_ && a.stages_ == b.stages_ &&
           a.addableSpeeds_ == b.addableSpeeds_ && a.edge_ == b.edge_ && a.s_ == b.s_;
}

} // namespace

void SignalPanelPresentation::Select(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= signals_.size()) return;
    const bool alreadySelected = signals_[static_cast<std::size_t>(index)].selected_;
    for (SignalPanelSignal& signal : signals_) signal.selected_ = false;
    signals_[static_cast<std::size_t>(index)].selected_ = !alreadySelected;
}

bool SignalPanelPresentation::Sync(std::vector<SignalPanelSignal> built) {
    if (built.size() == signals_.size()) {
        bool same = true;
        for (std::size_t i = 0; i < built.size() && same; ++i) same = SameContent(built[i], signals_[i]);
        if (same) return false;
    }
    std::unordered_map<std::string, const SignalPanelSignal*> previous;
    for (const SignalPanelSignal& signal : signals_) previous.emplace(signal.id_, &signal);
    for (SignalPanelSignal& signal : built) {
        const auto found = previous.find(signal.id_);
        if (found == previous.end()) continue;
        signal.selected_ = found->second->selected_;
        signal.shown_ = found->second->shown_;
        signal.left_ = found->second->left_;
        signal.top_ = found->second->top_;
    }
    signals_ = std::move(built);
    return true;
}

} // namespace rs::app::workspace

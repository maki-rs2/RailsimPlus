#include "InputSettingsPresentation.h"

#include <algorithm>

namespace rs::app::workspace {
bool InputSettingsPresentation::BeginCapture(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return false;
    CancelCapture();
    waitingRow_ = row;
    rows_[static_cast<std::size_t>(row)].waiting_ = true;
    return true;
}

void InputSettingsPresentation::CancelCapture() {
    for (InputSettingsRow& row : rows_) row.waiting_ = false;
    waitingRow_ = -1;
    pendingBinding_.clear();
}

InputSettingsCapture InputSettingsPresentation::Capture(const std::string& binding) {
    if (waitingRow_ < 0) return InputSettingsCapture::Ignored;
    if (binding == "Esc") {
        CancelCapture();
        return InputSettingsCapture::Cancelled;
    }
    if (binding.empty()) return InputSettingsCapture::Ignored;
    pendingBinding_ = binding;
    if (ConflictsWithFixed(waitingRow_, binding)) {
        pendingBinding_.clear();
        return InputSettingsCapture::Reserved;
    }
    return ConflictingRows(waitingRow_, binding).empty() ? InputSettingsCapture::Ready : InputSettingsCapture::Conflict;
}

namespace {
// 共通操作は各モードでも同時に使用できる。
bool UsedTogether(const std::string& a, const std::string& b) { return a == b || a == "common" || b == "common"; }

// 行が使う入力の表記。ダッシュ用の行は、主キーの修飾をCtrlだけにしたもの(例: Shift+W → Ctrl+W)も使う。
std::vector<std::string> EffectiveBindings(const std::string& binding, bool ctrlDash) {
    std::vector<std::string> result;
    if (binding.empty()) return result;
    result.push_back(binding);
    if (ctrlDash) result.push_back("Ctrl+" + binding.substr(binding.rfind('+') + 1));
    return result;
}

bool AnyEqual(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const std::string& left : a) {
        if (std::find(b.begin(), b.end(), left) != b.end()) return true;
    }
    return false;
}
} // namespace

std::vector<int> InputSettingsPresentation::ConflictingRows(int row, const std::string& binding) const {
    std::vector<int> result;
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || binding.empty()) return result;
    const InputSettingsRow& self = rows_[static_cast<std::size_t>(row)];
    const std::vector<std::string> mine = EffectiveBindings(binding, self.ctrlDash_);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const InputSettingsRow& other = rows_[index];
        if (index == static_cast<std::size_t>(row) || !UsedTogether(self.context_, other.context_)) continue;
        if (AnyEqual(mine, EffectiveBindings(other.binding_, other.ctrlDash_))) result.push_back(static_cast<int>(index));
    }
    return result;
}

bool InputSettingsPresentation::ConflictsWithFixed(int row, const std::string& binding) const {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || binding.empty()) return false;
    const InputSettingsRow& self = rows_[static_cast<std::size_t>(row)];
    const std::vector<std::string> mine = EffectiveBindings(binding, self.ctrlDash_);
    for (const InputSettingsFixedBinding& fixed : fixed_) {
        if (UsedTogether(self.context_, fixed.context_) && AnyEqual(mine, {fixed.binding_})) return true;
    }
    return false;
}

bool InputSettingsPresentation::Conflicts(int row, const std::string& binding) const {
    return ConflictsWithFixed(row, binding) || !ConflictingRows(row, binding).empty();
}

void InputSettingsPresentation::RefreshWarnings() {
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        InputSettingsRow& row = rows_[index];
        row.warning_ = row.binding_.empty() || Conflicts(static_cast<int>(index), row.binding_);
    }
}
} // namespace rs::app::workspace

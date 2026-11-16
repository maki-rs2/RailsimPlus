#include "TemplateSavePresentation.h"

#include <algorithm>
#include <cstddef>

#include "../../platform/text/AsciiCase.h"

namespace rs::app::workspace {

namespace {
// Windowsのファイルシステムは大文字小文字を区別しない。名前の判定もそれに合わせる。
bool SameTemplateName(const std::string& a, const std::string& b) {
    return rs::platform_text::ToAsciiLower(a) == rs::platform_text::ToAsciiLower(b);
}
} // namespace

void TemplateSavePresentation::Begin(TemplateSaveTarget target, const std::string& targetId) {
    Cancel();
    request_ = {target, targetId, {}};
    draft_ = sources_[static_cast<std::size_t>(target)].initialName_;
    layingEditing_ = target == TemplateSaveTarget::LayingSet;
    if (!layingEditing_) formationId_ = targetId;
}

bool TemplateSavePresentation::IsEditing() const {
    return layingEditing_ || !formationId_.empty();
}

std::optional<TemplateSaveRequest> TemplateSavePresentation::Commit() {
    if (!IsEditing() || draft_.empty()) return std::nullopt;
    request_.name_ = draft_;
    layingEditing_ = false;
    formationId_.clear();
    const std::vector<std::string>& names = sources_[static_cast<std::size_t>(request_.target_)].existingNames_;
    if (std::ranges::any_of(names, [this](const std::string& n) { return SameTemplateName(n, request_.name_); })) {
        overwritePending_ = true;
        overwriteMessage_ = "「" + request_.name_ + "」は既にあります。上書きしますか?";
        return std::nullopt;
    }
    const TemplateSaveRequest result = request_;
    Cancel();
    return result;
}

std::optional<TemplateSaveRequest> TemplateSavePresentation::ConfirmOverwrite() {
    if (!overwritePending_) return std::nullopt;
    const TemplateSaveRequest result = request_;
    Cancel();
    return result;
}

bool TemplateSavePresentation::Cancel() {
    const bool hadTransient = IsEditing() || overwritePending_;
    layingEditing_ = false;
    formationId_.clear();
    overwritePending_ = false;
    overwriteMessage_.clear();
    draft_.clear();
    request_ = {};
    return hadTransient;
}
} // namespace rs::app::workspace

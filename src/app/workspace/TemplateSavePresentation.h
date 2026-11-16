#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace rs::app::workspace {

enum class TemplateSaveTarget { LayingSet, Formation };

struct TemplateSaveSource {
    std::string initialName_;
    // 保存先にある名前(拡張子なし)。同名の判定は大文字小文字を区別しない(ファイルシステムの規則)。
    std::vector<std::string> existingNames_;
};

struct TemplateSaveRequest {
    TemplateSaveTarget target_;
    std::string targetId_;
    std::string name_;
};

struct TemplateSavePresentation {
    std::array<TemplateSaveSource, 2> sources_;
    std::string draft_;
    std::string formationId_;
    bool layingEditing_ = false;
    bool overwritePending_ = false;
    std::string overwriteMessage_;

    void Begin(TemplateSaveTarget target, const std::string& targetId);
    std::optional<TemplateSaveRequest> Commit();
    std::optional<TemplateSaveRequest> ConfirmOverwrite();
    bool Cancel();
    bool IsEditing() const;

private:
    TemplateSaveRequest request_{};
};
} // namespace rs::app::workspace

#include "ImGuiPathPicker.h"

#include <utility>

#include "imgui.h"

namespace rs::app {

void ImGuiPathPicker::BeginFrame(void* ownerWindow) {
    ownerWindow_ = ownerWindow;
    result_.reset();
    std::optional<std::string> picked;
    if (!picker_.TryTake(picked)) return;
    if (picked.has_value()) result_ = Result{requesterId_, std::move(*picked)};
    requesterId_ = 0;
}

std::optional<std::string> ImGuiPathPicker::Field(const char* label, const std::string& shown,
                                                  const PathPickSpec& spec) {
    ImGui::PushID(label);
    // 読み取り専用なので、バッファは書き換えられない(長いパスもスクロール・コピーできる)。
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputText(label, const_cast<char*>(shown.c_str()), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    const std::optional<std::string> picked = Button("参照…", spec, spec.fileType == nullptr ? shown : std::string());
    ImGui::PopID();
    return picked;
}

std::optional<std::string> ImGuiPathPicker::Button(const char* label, const PathPickSpec& spec,
                                                   const std::string& initialFolderUtf8) {
    const ImGuiID id = ImGui::GetID(label);
    ImGui::BeginDisabled(picker_.Open());
    if (ImGui::Button(label)) {
        const bool started = spec.fileType == nullptr
                                 ? picker_.Start(ownerWindow_, spec.titleUtf8, initialFolderUtf8)
                                 : picker_.StartOpenFile(ownerWindow_, spec.titleUtf8, *spec.fileType);
        if (started) requesterId_ = id;
    }
    ImGui::EndDisabled();

    if (result_.has_value() && result_->requesterId == id) {
        std::string path = std::move(result_->pathUtf8);
        result_.reset();
        return path;
    }
    return std::nullopt;
}

} // namespace rs::app

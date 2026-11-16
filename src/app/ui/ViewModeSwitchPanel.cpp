//
// ViewModeSwitchPanel.cpp - 「表示切り替え」パネル描画(main.cppから分離)
//
#include "UiPanels.h"
#include "ViewMode.h"

#include "imgui.h"

namespace rs::app {

void DrawViewModeSwitchPanel(ViewMode& mode) {
    ImGui::Begin("表示切り替え");
    int modeInt = static_cast<int>(mode);
    for (std::size_t order = 0; order < std::size(kViewModeCatalog); ++order) {
        for (const ViewModeTraits& traits : kViewModeCatalog) {
            if (traits.switchOrder == order) ImGui::RadioButton(traits.displayName, &modeInt, static_cast<int>(traits.mode));
        }
    }
    mode = static_cast<ViewMode>(modeInt);
    ImGui::End();
}

} // namespace rs::app

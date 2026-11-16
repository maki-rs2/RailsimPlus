#include "RmlUiWorkspaceView.h"

#include <windows.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>

#include "RmlUiContextMenu.h"
#include "RmlUiKeyboardRouter.h"
#include "../input/InputBindings.h"

namespace rs::app::rmlui {

// ワークスペースのキー入力(編成の一覧と車両の操作、撮影のEnter)。
bool RmlUiWorkspaceView::HandleListKey(ContextMenuKey key, Rml::Element* hover) {
    if ((key != ContextMenuKey::Up && key != ContextMenuKey::Down) || IsModalOverlayVisible()) return false;
    constexpr int kModifierMask = Rml::Input::KM_SHIFT | Rml::Input::KM_CTRL | Rml::Input::KM_ALT;
    if ((KeyModifierState() & kModifierMask) != 0) return false;
    return formationList_.HandleArrowKey(key == ContextMenuKey::Up ? -1 : +1, hover);
}

bool RmlUiWorkspaceView::HandleWorkspaceKeyDown(unsigned int virtualKey, Rml::Context& context, bool modalVisible) {
    if (modalVisible) return false;
    Rml::Element* target = context.GetFocusElement();
    if (target == nullptr) target = context.GetHoverElement();
    if (virtualKey == VK_RETURN) {
        if (cameraScreen_.TakePhotoByKey()) return true;
        for (Rml::Element* element = target; element != nullptr; element = element->GetParentNode()) {
            if (!element->IsClassSet("browser-card")) continue;
            const int entryId = element->GetAttribute<int>("data-entry-id", -1);
            return entryId >= 0 && formationList_.InsertPluginAfterFocused(entryId);
        }
    }
    return formationList_.HandleCarKeyDown(virtualKey, target);
}

} // namespace rs::app::rmlui

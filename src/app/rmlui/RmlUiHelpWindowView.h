#pragma once

#include <RmlUi/Core/DataModelHandle.h>

#include "../workspace/HelpWindowPresentation.h"

namespace rs::app {
class ToastQueue;
}

namespace rs::app::rmlui {

class RmlUiHelpWindowView {
public:
    explicit RmlUiHelpWindowView(ToastQueue& queue);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void Refresh(const workspace::WorkspaceSnapshot& snapshot);

private:
    ToastQueue& queue_;
    workspace::HelpWindowPresentation presentation_;
    Rml::DataModelHandle modelHandle_;
};

} // namespace rs::app::rmlui

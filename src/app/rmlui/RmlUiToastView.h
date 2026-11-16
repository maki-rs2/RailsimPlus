#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <RmlUi/Core/DataModelHandle.h>

#include "../action/ToastQueue.h"

namespace Rml {
class DataModelConstructor;
}

namespace rs::app::rmlui {

class RmlUiToastView {
public:
    RmlUiToastView();
    ~RmlUiToastView();
    RmlUiToastView(const RmlUiToastView&) = delete;
    RmlUiToastView& operator=(const RmlUiToastView&) = delete;

    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 通知のボタンを押したときに閉じるため、キューは非constで受ける。
    void Refresh(ToastQueue& queue);

private:
    void RunAction(int index);

    struct Model;
    std::unique_ptr<Model> model_;
    Rml::DataModelHandle modelHandle_;
    std::optional<std::size_t> shownVersion_;
    ToastQueue* queue_ = nullptr;
};

}  // namespace rs::app::rmlui

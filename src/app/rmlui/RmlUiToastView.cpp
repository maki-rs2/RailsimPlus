#include "RmlUiToastView.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {

namespace {

struct ToastRowModel {
    Rml::String text;
    Rml::String severity;
    // ボタンの文言(空ならボタンなし)。
    Rml::String action;

    bool operator==(const ToastRowModel&) const = default;
};

constexpr const char* kToastsKey = "toasts";

Rml::String SeverityClass(ToastSeverity severity) {
    switch (severity) {
    case ToastSeverity::Info: return "info";
    case ToastSeverity::Warning: return "warning";
    case ToastSeverity::Error: return "error";
    }
    return "info";
}

}  // namespace

struct RmlUiToastView::Model {
    Rml::Vector<ToastRowModel> toasts;
};

RmlUiToastView::RmlUiToastView() : model_(std::make_unique<Model>()) {}
RmlUiToastView::~RmlUiToastView() = default;

void RmlUiToastView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<ToastRowModel> handle = constructor.RegisterStruct<ToastRowModel>()) {
        handle.RegisterMember("text", &ToastRowModel::text);
        handle.RegisterMember("severity", &ToastRowModel::severity);
        handle.RegisterMember("action", &ToastRowModel::action);
    }
    constructor.RegisterArray<Rml::Vector<ToastRowModel>>();
    constructor.Bind(kToastsKey, &model_->toasts);
    constructor.BindEventCallback("toast_action", [this](Rml::DataModelHandle, Rml::Event& event,
                                                          const Rml::VariantList& arguments) {
        event.StopPropagation();
        RunAction(EventArgumentAt<int>(arguments, 0, -1));
    });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiToastView::RunAction(int index) {
    if (queue_ == nullptr || index < 0 || static_cast<std::size_t>(index) >= queue_->Items().size()) return;
    // 処理の中で通知が増減しても壊れないよう、呼ぶ前に取り出して閉じる。
    const std::function<void()> onAction = queue_->Items()[static_cast<std::size_t>(index)].onAction;
    queue_->Dismiss(static_cast<std::size_t>(index));
    if (onAction) onAction();
}

void RmlUiToastView::Refresh(ToastQueue& queue) {
    queue_ = &queue;
    if (!modelHandle_ || (shownVersion_.has_value() && *shownVersion_ == queue.Version())) return;
    shownVersion_ = queue.Version();

    Rml::Vector<ToastRowModel> rows;
    rows.reserve(queue.Items().size());
    for (const ToastItem& item : queue.Items()) rows.push_back({item.text, SeverityClass(item.severity), item.onAction ? item.actionLabel : Rml::String()});
    UpdateVariable(modelHandle_, kToastsKey, model_->toasts, rows);
}

}  // namespace rs::app::rmlui

#include "RmlUiRecoveryView.h"

#include <utility>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"

namespace rs::app::rmlui {

struct RecoveryCardModel {
    Rml::String name;
    Rml::String date;
};

// データモデルへ出す値の入れ物(定義はここだけ)。
struct RecoveryModel {
    Rml::Vector<RecoveryCardModel> cards;
};

namespace {
constexpr const char* kCardsKey = "recoveryCards";
} // namespace

RmlUiRecoveryView::RmlUiRecoveryView() : model_(std::make_unique<RecoveryModel>()) {}
RmlUiRecoveryView::~RmlUiRecoveryView() = default;

void RmlUiRecoveryView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<RecoveryCardModel> handle = constructor.RegisterStruct<RecoveryCardModel>()) {
        handle.RegisterMember("name", &RecoveryCardModel::name);
        handle.RegisterMember("date", &RecoveryCardModel::date);
    }
    constructor.RegisterArray<Rml::Vector<RecoveryCardModel>>();
    constructor.Bind(kCardsKey, &model_->cards);

    const auto bindCommand = [this, &constructor](const Rml::String& name, RecoveryCommand::Kind kind) {
        constructor.BindEventCallback(
            name, [this, kind](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                SetCommand(kind, FirstEventArgument<int>(arguments, 0));
            });
    };
    bindCommand("recovery_restore", RecoveryCommand::Kind::Restore);
    bindCommand("recovery_discard", RecoveryCommand::Kind::Discard);
    bindCommand("recovery_normal", RecoveryCommand::Kind::NormalStart);
    bindCommand("recovery_safe", RecoveryCommand::Kind::SafeMode);

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiRecoveryView::SetCards(const std::vector<RecoveryCardContent>& cards) {
    model_->cards.clear();
    for (const RecoveryCardContent& card : cards) model_->cards.push_back({card.name, card.date});
    if (modelHandle_) modelHandle_.DirtyVariable(kCardsKey);
}

std::optional<RecoveryCommand> RmlUiRecoveryView::TakeCommand() {
    return std::exchange(command_, std::nullopt);
}

void RmlUiRecoveryView::SetCommand(RecoveryCommand::Kind kind, int index) {
    if (index < 0) return;
    command_ = RecoveryCommand{kind, static_cast<std::size_t>(index)};
}

} // namespace rs::app::rmlui

#include "RmlUiPluginSettingsView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Types.h>

#include <spdlog/spdlog.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiEnumKey.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdatePhase.h"
#include "RmlUiUpdateVariable.h"
#include "../action/SceneEntitySwitchAction.h"
#include "../action/SignalPluginRules.h"
#include "../action/SwitchList.h"

namespace rs::app::rmlui {

// スイッチ1行。kindは行の出し方(SwitchRowKindの小文字名: choice/header/spacer)。valueTextはコンボで
// 選択中の添字(selectのdata-valueに合わせて文字列で持つ)。
struct PluginSwitchRowModel {
    Rml::String name;
    Rml::Vector<Rml::String> entries;
    Rml::String valueText;
    Rml::String kind;
    // 閉塞の現示で決まるスイッチ。コンボの代わりにlinkedTextを出し、編集させない。
    bool linked = false;
    bool operator==(const PluginSwitchRowModel&) const = default;
};

struct PluginSwitchesModel {
    Rml::Vector<PluginSwitchRowModel> rows;
    // 連動中の行に出す文(全行共通)。
    Rml::String linkedText = kSignalSwitchLinkedToBlockText;
};

namespace {

// 重ね表示の名前"pluginSettings"(OverlayCatalog)と同じ名前の変数は作れないので、行の配列は別の名前にする。
constexpr const char* kRowsKey = "pluginSwitchRows";
constexpr const char* kLinkedTextKey = "pluginSwitchLinkedText";

PluginSwitchRowModel ToRowModel(const SwitchRowViewModel& row) {
    PluginSwitchRowModel model;
    model.name = row.name;
    model.entries.assign(row.entries.begin(), row.entries.end());
    model.valueText = std::to_string(row.value);
    model.kind = ToEnumKey(GetSwitchRowKind(row));
    model.linked = row.linkedToBlock;
    return model;
}

std::string ReadControlValue(Rml::Element& element) {
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element);
    return control != nullptr ? control->GetValue() : std::string();
}

} // namespace

RmlUiPluginSettingsView::RmlUiPluginSettingsView() : model_(std::make_unique<PluginSwitchesModel>()) {}
RmlUiPluginSettingsView::~RmlUiPluginSettingsView() = default;

void RmlUiPluginSettingsView::Attach(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer,
                                     rs::editor::Editor& editor) {
    sceneRegistry_ = &sceneRegistry;
    sceneRenderer_ = &sceneRenderer;
    editor_ = &editor;
}

void RmlUiPluginSettingsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<PluginSwitchRowModel> handle = constructor.RegisterStruct<PluginSwitchRowModel>()) {
        handle.RegisterMember("name", &PluginSwitchRowModel::name);
        handle.RegisterMember("entries", &PluginSwitchRowModel::entries);
        handle.RegisterMember("valueText", &PluginSwitchRowModel::valueText);
        handle.RegisterMember("kind", &PluginSwitchRowModel::kind);
        handle.RegisterMember("linked", &PluginSwitchRowModel::linked);
    }
    constructor.RegisterArray<Rml::Vector<PluginSwitchRowModel>>();
    constructor.Bind(kRowsKey, &model_->rows);
    constructor.Bind(kLinkedTextKey, &model_->linkedText);

    constructor.BindEventCallback("commit_switch_row",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnCommitRow(event, arguments);
                                  });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiPluginSettingsView::Refresh(const rs::world::Guid& targetEntityId) {
    if (sceneRegistry_ == nullptr || sceneRenderer_ == nullptr || !modelHandle_) return;
    targetEntityId_ = targetEntityId;

    const std::vector<SwitchRowViewModel> viewRows =
        SceneEntitySwitchAction(targetEntityId_, *sceneRegistry_, *sceneRenderer_, editor_).ViewModel().rows;
    Rml::Vector<PluginSwitchRowModel> rows;
    rows.reserve(viewRows.size());
    for (const SwitchRowViewModel& row : viewRows) rows.push_back(ToRowModel(row));

    UpdateVariable(modelHandle_, kRowsKey, model_->rows, rows);
}

void RmlUiPluginSettingsView::OnCommitRow(Rml::Event& event, const Rml::VariantList& arguments) {
    if (sceneRegistry_ == nullptr || sceneRenderer_ == nullptr) return;
    // コンボはデータ結線で値を入れたときや、選択肢を作り直したときにもchangeを出す。それを書き込むと、
    // 開いただけ・対象を切り替えただけで個体の値が変わってしまうので、ユーザーの操作(Updateの外)だけを受ける。
    if (IsInsideContextUpdate()) return;
    const int rowIndex = FirstEventArgument<int>(arguments, -1);
    Rml::Element* const element = event.GetCurrentElement();
    if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= model_->rows.size() || element == nullptr) {
        spdlog::warn("[rmlui] commit_switch_rowの行番号が範囲外です: {}", rowIndex);
        return;
    }
    const std::size_t row = static_cast<std::size_t>(rowIndex);
    PluginSwitchRowModel& rowModel = model_->rows[row];

    // コンボの値は選択肢の添字。
    const std::string text = ReadControlValue(*element);

    SceneEntitySwitchAction action(targetEntityId_, *sceneRegistry_, *sceneRenderer_, editor_);
    const std::vector<SwitchRowViewModel> before = action.ViewModel().rows;
    int parsed = 0;
    // 連動中の行は書かない(信号側でも弾くが、ここでも試みない)。
    if (row < before.size() && !before[row].linkedToBlock && ParseIntText(text, parsed) &&
        parsed != before[row].value) {
        action.SetSwitch(row, static_cast<std::int32_t>(parsed));
    }

    // 反映後の値(書けなければ今の値)を、コンボとデータモデルへ戻す(丸めた値の表示合わせ)。
    const std::vector<SwitchRowViewModel> after = action.ViewModel().rows;
    if (row >= after.size()) return;
    const std::string current = std::to_string(after[row].value);
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
    if (control != nullptr && control->GetValue() != current) control->SetValue(current);
    rowModel.valueText = current;
}

} // namespace rs::app::rmlui

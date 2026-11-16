#include "RmlUiViewerView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Types.h>

#include <spdlog/spdlog.h>

#include "../action/PluginInfoAction.h"
#include "../action/ViewerSettingsAction.h"
#include "../plugin_session/PluginSession.h"
#include "../plugin_session/ViewerState.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiEnumKey.h"
#include "RmlUiFieldCommit.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdatePhase.h"
#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {

namespace {

// 自動計算のシステムスイッチの行(値の表示だけ)の種類。ほかの種類は入力部品(SystemSwitchWidget)の小文字名。
constexpr const char* kReadOnlyKind = "readonly";

constexpr const char* kLoadedKey = "viewerLoaded";
constexpr const char* kPluginNameKey = "viewerPluginName";
constexpr const char* kTypeNameKey = "viewerTypeName";
constexpr const char* kAuthorKey = "viewerAuthor";
constexpr const char* kPartCountKey = "viewerPartCount";
constexpr const char* kMissingFilesKey = "viewerMissingFiles";
constexpr const char* kLightOnKey = "viewerLightOn";
constexpr const char* kSystemRowsKey = "viewerSystemRows";

} // namespace

// システムスイッチ1行。kindは入力部品の出し方(checkbox/slider/number/readonly)。
struct ViewerSystemRowModel {
    Rml::String name;
    Rml::String kind;
    int value = 0;
    // 数値欄に出す文字列。
    Rml::String valueText;
    int minValue = 0;
    int maxValue = 0;
    bool operator==(const ViewerSystemRowModel&) const = default;
};

struct ViewerModel {
    bool loaded = false;
    Rml::String pluginName;
    Rml::String typeName;
    Rml::String author;
    int partCount = 0;
    int missingFiles = 0;
    bool lightOn = true;
    Rml::Vector<ViewerSystemRowModel> systemRows;
};

namespace {

ViewerSystemRowModel ToRowModel(const ViewerSystemSwitchRow& row) {
    ViewerSystemRowModel model;
    model.name = row.name;
    model.kind = row.readOnly ? kReadOnlyKind : ToEnumKey(row.widget);
    model.value = static_cast<int>(row.value);
    model.valueText = std::to_string(row.value);
    model.minValue = static_cast<int>(row.minValue);
    model.maxValue = static_cast<int>(row.maxValue);
    return model;
}

} // namespace

RmlUiViewerView::RmlUiViewerView() : model_(std::make_unique<ViewerModel>()) {}
RmlUiViewerView::~RmlUiViewerView() = default;

bool RmlUiViewerView::CreateDataModel(Rml::Context& context) {
    Rml::DataModelConstructor constructor = context.CreateDataModel("viewer");
    if (!constructor) return false;

    if (Rml::StructHandle<ViewerSystemRowModel> handle = constructor.RegisterStruct<ViewerSystemRowModel>()) {
        handle.RegisterMember("name", &ViewerSystemRowModel::name);
        handle.RegisterMember("kind", &ViewerSystemRowModel::kind);
        handle.RegisterMember("value", &ViewerSystemRowModel::value);
        handle.RegisterMember("valueText", &ViewerSystemRowModel::valueText);
        handle.RegisterMember("minValue", &ViewerSystemRowModel::minValue);
        handle.RegisterMember("maxValue", &ViewerSystemRowModel::maxValue);
    }
    constructor.RegisterArray<Rml::Vector<ViewerSystemRowModel>>();
    // 文字列の配列。プラグイン設定の部品(スイッチの選択肢)が使うので、それより先に1回だけ登録する。
    constructor.RegisterArray<Rml::Vector<Rml::String>>();
    constructor.Bind(kLoadedKey, &model_->loaded);
    constructor.Bind(kPluginNameKey, &model_->pluginName);
    constructor.Bind(kTypeNameKey, &model_->typeName);
    constructor.Bind(kAuthorKey, &model_->author);
    constructor.Bind(kPartCountKey, &model_->partCount);
    constructor.Bind(kMissingFilesKey, &model_->missingFiles);
    constructor.Bind(kLightOnKey, &model_->lightOn);
    constructor.Bind(kSystemRowsKey, &model_->systemRows);

    constructor.BindEventCallback("viewer_set_light",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSetLight(arguments);
                                  });
    constructor.BindEventCallback("viewer_set_system",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnSetSystemSwitch(event, arguments);
                                  });
    pluginSettings_.RegisterModel(constructor);

    modelHandle_ = constructor.GetModelHandle();
    return true;
}

void RmlUiViewerView::Attach(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer,
                             rs::editor::Editor& editor, ViewerState& viewer, SessionProvider sessionProvider) {
    pluginSettings_.Attach(sceneRegistry, sceneRenderer, editor);
    viewer_ = &viewer;
    sessionProvider_ = std::move(sessionProvider);
}

void RmlUiViewerView::Refresh(const rs::world::Guid& pluginEntityId) {
    if (viewer_ == nullptr || !sessionProvider_ || !modelHandle_) return;
    pluginSettings_.Refresh(pluginEntityId);

    PluginSession* const session = sessionProvider_();
    const PluginInfoViewModel info =
        session != nullptr ? PluginInfoAction(*session).ViewModel() : PluginInfoViewModel{};
    UpdateVariable(modelHandle_, kLoadedKey, model_->loaded, info.loaded);
    UpdateVariable(modelHandle_, kPluginNameKey, model_->pluginName, Rml::String(info.pluginName));
    UpdateVariable(modelHandle_, kTypeNameKey, model_->typeName, Rml::String(info.typeName));
    UpdateVariable(modelHandle_, kAuthorKey, model_->author, Rml::String(info.author));
    UpdateVariable(modelHandle_, kPartCountKey, model_->partCount, static_cast<int>(info.partCount));
    UpdateVariable(modelHandle_, kMissingFilesKey, model_->missingFiles, info.missingFiles);

    ViewerSettingsViewModel settings;
    if (session != nullptr) settings = ViewerSettingsAction(*session, *viewer_).ViewModel();
    UpdateVariable(modelHandle_, kLightOnKey, model_->lightOn, settings.lightOn);
    Rml::Vector<ViewerSystemRowModel> rows;
    rows.reserve(settings.systemSwitches.size());
    for (const ViewerSystemSwitchRow& row : settings.systemSwitches) rows.push_back(ToRowModel(row));
    UpdateVariable(modelHandle_, kSystemRowsKey, model_->systemRows, rows);
}

void RmlUiViewerView::OnSetLight(const Rml::VariantList& arguments) {
    PluginSession* const session = sessionProvider_ ? sessionProvider_() : nullptr;
    if (session == nullptr || viewer_ == nullptr) return;
    ViewerSettingsAction(*session, *viewer_).SetLightOn(FirstEventArgument<int>(arguments, 1) != 0);
}

void RmlUiViewerView::OnSetSystemSwitch(Rml::Event& event, const Rml::VariantList& arguments) {
    PluginSession* const session = sessionProvider_ ? sessionProvider_() : nullptr;
    // データ結線で値を入れたときにもchangeが出る。ユーザーの操作(Updateの外)だけを受ける。
    if (session == nullptr || viewer_ == nullptr || IsInsideContextUpdate()) return;
    const int rowIndex = FirstEventArgument<int>(arguments, -1);
    Rml::Element* const element = event.GetCurrentElement();
    if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= model_->systemRows.size() || element == nullptr) {
        spdlog::warn("[rmlui] viewer_set_systemの行番号が範囲外です: {}", rowIndex);
        return;
    }
    const ViewerSystemRowModel& row = model_->systemRows[static_cast<std::size_t>(rowIndex)];
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);

    int value = 0;
    if (row.kind == ToEnumKey(SystemSwitchWidget::Checkbox)) {
        value = element->HasAttribute("checked") ? 1 : 0;
    } else if (control != nullptr && row.kind != kReadOnlyKind) {
        // Esc で取り消した欄と、数に読めない入力は確定しない(表示は今の値へ戻す)。
        if (ConsumeFieldCancel(element) || !ParseIntText(control->GetValue(), value)) {
            control->SetValue(row.valueText);
            return;
        }
    } else {
        return;
    }
    if (value == row.value) return;
    ViewerSettingsAction(*session, *viewer_).SetSystemSwitch(row.name, static_cast<std::int32_t>(value));
}

} // namespace rs::app::rmlui

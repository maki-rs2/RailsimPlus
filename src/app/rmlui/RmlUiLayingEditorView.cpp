#include "RmlUiLayingEditorView.h"

#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <spdlog/spdlog.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiEnumKey.h"
#include "RmlUiLayingFields.h"
#include "RmlUiUpdateVariable.h"
#include "../action/TrackLayingAction.h"
#include "../action/TrackLayingPreviewColor.h"
#include "../action/TrackLayingText.h"
#include "../../editor/tool/Editor.h"

namespace rs::app::rmlui {
namespace {

// 登録(RegisterModel)と更新(Refresh*)の両方で使うバインド名。
constexpr const char* kTransitionEnabledKey = "layingTransitionEnabled";
constexpr const char* kTrackSpacingDisabledKey = "layingTrackSpacingDisabled";
constexpr const char* kMinimizedKey = "layingMinimized";
constexpr const char* kNoticeLinesKey = "layingNoticeLines";
constexpr const char* kStatusLinesKey = "layingStatusLines";

Rml::Vector<LayingLineModel> ToLineModels(const std::vector<TextLine>& lines) {
    Rml::Vector<LayingLineModel> models;
    models.reserve(lines.size());
    for (const TextLine& line : lines) {
        models.push_back({line.text, ToEnumKey(line.severity)});
    }
    return models;
}

// 欄の入力欄から値を文字列で読む。Toggleはchecked属性の有無("1"/"0")、ほかは入力欄の現在の値。
std::string ReadElementValue(const LayingField& field, Rml::Element& element) {
    if (field.kind == LayingFieldKind::Toggle) return element.HasAttribute("checked") ? "1" : "0";
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element);
    return control != nullptr ? control->GetValue() : std::string();
}

void RegisterModelTypes(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<LayingLineModel> handle = constructor.RegisterStruct<LayingLineModel>()) {
        handle.RegisterMember("text", &LayingLineModel::text);
        handle.RegisterMember("severity", &LayingLineModel::severity);
    }
    constructor.RegisterArray<Rml::Vector<LayingLineModel>>();
}

} // namespace

RmlUiLayingEditorView::RmlUiLayingEditorView() : model_(std::make_unique<LayingEditorModel>()) {}
RmlUiLayingEditorView::~RmlUiLayingEditorView() = default;

void RmlUiLayingEditorView::Attach(rs::editor::Editor& editor, const rs::editor::PlacePreview& preview) {
    editor_ = &editor;
    preview_ = &preview;
}

void RmlUiLayingEditorView::RegisterModel(Rml::DataModelConstructor& constructor) {
    RegisterModelTypes(constructor);

    // 固定の値(コンボの選択肢)は、ここで1回だけ入れる。
    for (const TransitionChoice& choice : kTransitionChoices) {
        model_->transitionLabels.push_back(choice.label);
    }
    constructor.Bind("layingTransitionLabels", &model_->transitionLabels);

    // 入力欄の変数は表から結ぶ(文字を持つ欄は表のメンバ、トグルは下の2つ)。
    for (const LayingField& field : LayingFields()) {
        if (field.text != nullptr) constructor.Bind(field.key, &(model_.get()->*field.text));
    }
    constructor.Bind(kLayingAngleSnapEnabledKey, &model_->angleSnapEnabled);
    constructor.Bind(kTransitionEnabledKey, &model_->transitionEnabled);
    constructor.Bind(kTrackSpacingDisabledKey, &model_->trackSpacingDisabled);
    constructor.Bind(kMinimizedKey, &model_->minimized);
    constructor.Bind(kNoticeLinesKey, &model_->noticeLines);
    constructor.Bind(kStatusLinesKey, &model_->statusLines);

    constructor.BindEventCallback("commit_laying_field",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnCommitField(event, arguments);
                                  });
    constructor.BindEventCallback("focus_laying_field",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnFocusField(arguments);
                                  });
    constructor.BindEventCallback("key_laying_field",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnKeyField(event, arguments);
                                  });
    // バナーの元に戻す・やり直す(全モードで共通。エディタの操作の履歴をたどる)。
    constructor.Bind("canUndo", &model_->canUndo);
    constructor.Bind("canRedo", &model_->canRedo);
    constructor.BindEventCallback("edit_history", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
        if (editor_ == nullptr || preview_ == nullptr) return;
        TrackLayingAction action(*editor_, *preview_);
        if (FirstEventArgument<bool>(arguments, false)) action.Redo();
        else action.Undo();
    });
    constructor.BindEventCallback("toggle_laying_minimized",
                                  [this](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
                                      model_->minimized = !model_->minimized;
                                      handle.DirtyVariable(kMinimizedKey);
                                  });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiLayingEditorView::Refresh() {
    if (editor_ == nullptr || !modelHandle_) return;
    const TrackLayingViewModel viewModel = TrackLayingAction(*editor_, *preview_).ViewModel();
    RefreshSettings(viewModel);
    RefreshStatus(viewModel);
    UpdateVariable(modelHandle_, "canUndo", model_->canUndo, editor_->CanUndo());
    UpdateVariable(modelHandle_, "canRedo", model_->canRedo, editor_->CanRedo());
}

void RmlUiLayingEditorView::ResetInputFocus() {
    focusedField_.clear();
}

void RmlUiLayingEditorView::RefreshSettings(const TrackLayingViewModel& viewModel) {
    LayingEditorModel& model = *model_;
    for (const LayingField& field : LayingFields()) {
        if (field.text == nullptr || focusedField_ == field.key) continue; // フォーカス中の欄は上書きしない
        UpdateVariable(modelHandle_, field.key, model.*field.text, field.format(viewModel));
    }
    UpdateVariable(modelHandle_, kLayingAngleSnapEnabledKey, model.angleSnapEnabled, viewModel.angleSnapEnabled);
    UpdateVariable(modelHandle_, kTransitionEnabledKey, model.transitionEnabled, viewModel.transitionEnabled);
    // 敷設本数が1(単線)のときは線路間隔を無効表示にする(値は保持する)。
    UpdateVariable(modelHandle_, kTrackSpacingDisabledKey, model.trackSpacingDisabled, viewModel.trackCount <= 1);
}

void RmlUiLayingEditorView::RefreshStatus(const TrackLayingViewModel& viewModel) {
    LayingEditorModel& model = *model_;
    UpdateVariable(modelHandle_, kNoticeLinesKey, model.noticeLines,
                   ToLineModels(TransitionNoticeLines(viewModel)));
    // プレビューの解を先に、そのあとに複線・開始ノード・終点選択の状態を並べる。
    std::vector<TextLine> statusLines = ArcPreviewLines(viewModel.preview);
    for (TextLine& line : LayingStatusLines(viewModel)) statusLines.push_back(std::move(line));
    UpdateVariable(modelHandle_, kStatusLinesKey, model.statusLines, ToLineModels(statusLines));
}

void RmlUiLayingEditorView::OnCommitField(Rml::Event& event, const Rml::VariantList& arguments) {
    if (editor_ == nullptr) return;
    const Rml::String key = FirstEventArgument<Rml::String>(arguments, Rml::String());
    const LayingField* const field = FindLayingField(key);
    Rml::Element* const element = event.GetCurrentElement();
    if (field == nullptr || element == nullptr) {
        spdlog::warn("[rmlui] commit_laying_fieldの引数が未知です: {}", key);
        return;
    }

    const bool isBlur = event.GetId() == Rml::EventId::Blur;
    if (isBlur) {
        if (focusedField_ == key) focusedField_.clear();
        if (suppressBlurCommit_) return; // Escの取り消し(確定しない)
    }
    // 入力途中(changeでlinebreakなし)は反映しない。変数には打った文字を写し、ほかの更新で
    // 入力中の文字が消されないようにする。Enter(linebreak)・blur・トグル・コンボだけ反映する。
    if (field->kind == LayingFieldKind::Text && !isBlur && !event.GetParameter<bool>("linebreak", false)) {
        model_.get()->*field->text = ReadElementValue(*field, *element);
        return;
    }
    ApplyField(*field, *element);
}

void RmlUiLayingEditorView::OnFocusField(const Rml::VariantList& arguments) {
    focusedField_ = FirstEventArgument<Rml::String>(arguments, Rml::String());
}

void RmlUiLayingEditorView::OnKeyField(Rml::Event& event, const Rml::VariantList& arguments) {
    if (editor_ == nullptr) return;
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    const Rml::String key = FirstEventArgument<Rml::String>(arguments, Rml::String());
    const LayingField* const field = FindLayingField(key);
    Rml::Element* const element = event.GetCurrentElement();
    if (field == nullptr || field->kind != LayingFieldKind::Text || element == nullptr) return;

    // 入力を取り消して(今の値の表示に戻して)フォーカスを外す。blurでは確定しない。
    ResetText(*field, *element);
    suppressBlurCommit_ = true;
    element->Blur();
    suppressBlurCommit_ = false;
    focusedField_.clear();
    event.StopPropagation();
}

void RmlUiLayingEditorView::ApplyField(const LayingField& field, Rml::Element& element) {
    TrackLayingAction action(*editor_, *preview_);
    const std::string value = ReadElementValue(field, element);
    if (field.kind != LayingFieldKind::Text) {
        if (!field.apply(action, value)) spdlog::warn("[rmlui] 敷設エディタの値を読めません: {} = {}", field.key, value);
        return;
    }
    // 文字入力欄: 今の値の書式と同じ文字は「編集なし」なので反映しない(書式の丸めで値を書き換えない)。
    // 数として読めないときも含め、最後は反映後の値(読めなければ今の値)の表示に戻す。
    if (value != field.format(action.ViewModel())) field.apply(action, value);
    ResetText(field, element);
}

void RmlUiLayingEditorView::ResetText(const LayingField& field, Rml::Element& element) {
    const std::string text = field.format(TrackLayingAction(*editor_, *preview_).ViewModel());
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element);
    if (control != nullptr && control->GetValue() != text) control->SetValue(text);
    model_.get()->*field.text = text;
}

} // namespace rs::app::rmlui

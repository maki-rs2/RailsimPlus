#include "RmlUiLayoutFilesView.h"

#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Core.h>

#include "RmlUiDataEventArgs.h"
#include "../../platform/file/ShellFileTypes.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/SearchTerms.h"

namespace rs::app::rmlui {

struct LayoutFileCardModel {
    Rml::String name;
    Rml::String date;
    Rml::String image; // サムネイルの画像の名前。無ければ空(枠だけ出す)
};

// データモデルへ出す値の入れ物(定義はここだけ)。
struct LayoutFilesModel {
    Rml::String title;
    bool saveMode = false;
    Rml::Vector<LayoutFileCardModel> recentCards;
    Rml::Vector<LayoutFileCardModel> folderCards;
    Rml::String name;
    bool nameValid = false;
    bool picking = false;
    Rml::Vector<LayoutFileCardModel> rs2Cards;
    bool rs2Available = false;
    Rml::String rs2Search;
    Rml::String tab; // "own"(保存したレイアウト)か "rs2"(RailSim2のレイアウト)
};

namespace {

constexpr const char* kNameInputId = "layout-file-name-input";
constexpr const char* kTitleLoad = "ロード";
constexpr const char* kTitleSaveAs = "名前を付けて保存";
constexpr const char* kPickerTitle = "レイアウトを開く";
constexpr const char* kTabOwn = "own";
constexpr const char* kTabRs2 = "rs2";
constexpr const char* kScrollSelector = ".layout-files-scroll";
constexpr const char* kImageSourcePrefix = "ltimg:";
// 1フレームに受け取るサムネイルの枚数(テクスチャの作成が1フレームに集まらないように)。
constexpr std::size_t kThumbnailsPerFrame = 4;

Rml::Vector<LayoutFileCardModel> ToCards(const std::vector<LayoutFileEntry>& entries,
                                         const std::map<std::string, std::string>& imageSourceByPath) {
    Rml::Vector<LayoutFileCardModel> cards;
    cards.reserve(entries.size());
    for (const LayoutFileEntry& entry : entries) {
        const std::map<std::string, std::string>::const_iterator image = imageSourceByPath.find(entry.pathUtf8);
        cards.push_back({entry.nameUtf8, entry.dateUtf8, image != imageSourceByPath.end() ? image->second : Rml::String()});
    }
    return cards;
}

} // namespace

RmlUiLayoutFilesView::RmlUiLayoutFilesView() : model_(std::make_unique<LayoutFilesModel>()) {}
RmlUiLayoutFilesView::~RmlUiLayoutFilesView() = default;

void RmlUiLayoutFilesView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<LayoutFileCardModel> handle = constructor.RegisterStruct<LayoutFileCardModel>()) {
        handle.RegisterMember("name", &LayoutFileCardModel::name);
        handle.RegisterMember("date", &LayoutFileCardModel::date);
        handle.RegisterMember("image", &LayoutFileCardModel::image);
    }
    constructor.RegisterArray<Rml::Vector<LayoutFileCardModel>>();
    constructor.Bind("layoutFilesTitle", &model_->title);
    constructor.Bind("layoutFilesSaveMode", &model_->saveMode);
    constructor.Bind("layoutRecentCards", &model_->recentCards);
    constructor.Bind("layoutFolderCards", &model_->folderCards);
    constructor.Bind("layoutFileName", &model_->name);
    constructor.Bind("layoutFileNameValid", &model_->nameValid);
    constructor.Bind("layoutFilesPicking", &model_->picking);
    constructor.Bind("layoutRs2Cards", &model_->rs2Cards);
    constructor.Bind("layoutRs2Available", &model_->rs2Available);
    constructor.Bind("layoutRs2Search", &model_->rs2Search);
    constructor.Bind("layoutFilesTab", &model_->tab);

    constructor.BindEventCallback("layout_recent_click",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnCardClick(recent_, FirstEventArgument<int>(arguments, -1));
                                  });
    constructor.BindEventCallback("layout_folder_click",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnCardClick(folder_, FirstEventArgument<int>(arguments, -1));
                                  });
    constructor.BindEventCallback("layout_rs2_click",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      const int index = FirstEventArgument<int>(arguments, -1);
                                      if (index < 0 || static_cast<std::size_t>(index) >= rs2Shown_.size()) return;
                                      const LayoutFileEntry& entry = rs2_[rs2Shown_[static_cast<std::size_t>(index)]];
                                      command_ = LayoutFilesCommand{LayoutFilesCommand::Kind::LoadFile, entry.pathUtf8};
                                  });
    constructor.BindEventCallback("select_layout_files_tab",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSelectTab(arguments);
                                  });
    constructor.BindEventCallback("layout_rs2_search",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnRs2Search(event);
                                  });
    constructor.BindEventCallback("layout_name_change",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnNameChange(event);
                                  });
    constructor.BindEventCallback("layout_name_key",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnNameKey(event);
                                  });
    constructor.BindEventCallback("layout_name_submit",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SubmitName(); });

    constructor.BindEventCallback("layout_open_other",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnOpenOther(); });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiLayoutFilesView::SetContent(LayoutFilesMode mode, std::vector<LayoutFileEntry> recent,
                                      std::vector<LayoutFileEntry> folder, const std::string& initialName,
                                      std::vector<LayoutFileEntry> rs2) {
    mode_ = mode;
    rs2_ = std::move(rs2);
    model_->rs2Search.clear();
    model_->rs2Available = !rs2_.empty();
    model_->tab = kTabOwn;
    recent_ = std::move(recent);
    folder_ = std::move(folder);
    command_.reset();
    ReleaseImages();
    // 前に開いたときの受付と結果は捨てる。
    ++imageGeneration_;
    thumbnailLoader_.ClearPending();
    thumbnailLoader_.Drain(SIZE_MAX);
    for (const std::vector<LayoutFileEntry>* entries : {&recent_, &folder_}) {
        for (const LayoutFileEntry& entry : *entries) thumbnailLoader_.Post({imageGeneration_, entry.pathUtf8});
    }
    model_->title = mode == LayoutFilesMode::SaveAs ? kTitleSaveAs : kTitleLoad;
    model_->saveMode = mode == LayoutFilesMode::SaveAs;
    RebuildCards();
    RebuildRs2Cards();
    pendingFocus_ = mode == LayoutFilesMode::SaveAs;
    if (modelHandle_) modelHandle_.DirtyAllVariables();
    SetName(initialName);
}

void RmlUiLayoutFilesView::Refresh(Rml::ElementDocument& document) {
    // 画面を閉じても、見えなくなった入力欄にフォーカスが残ると、キー入力がずっと文字入力として扱われ、
    // ビューポートのショートカット(Scene切替など)が止まる。見えなくなったらフォーカスを外す。
    if (Rml::Context* const context = document.GetContext()) {
        Rml::Element* const focus = context->GetFocusElement();
        if (focus != nullptr && document.Contains(focus) && !focus->IsVisible(true)) focus->Blur();
    }
    if (std::exchange(pendingScrollTop_, false)) {
        if (Rml::Element* const scroll = document.QuerySelector(kScrollSelector)) scroll->SetScrollTop(0.0f);
    }
    // ダイアログで選ばれたら、カードを押したときと同じ扱いにする。
    std::optional<std::string> picked;
    if (picker_.TryTake(picked) && picked.has_value()) {
        command_ = LayoutFilesCommand{LayoutFilesCommand::Kind::LoadFile, *picked};
    }
    if (model_->picking != picker_.Open()) {
        model_->picking = picker_.Open();
        if (modelHandle_) modelHandle_.DirtyVariable("layoutFilesPicking");
    }

    // 読めた順にカードへ絵を入れる。撮っていないファイルは枠のまま。
    bool imageAdded = false;
    for (LayoutThumbnailResult& result : thumbnailLoader_.Drain(kThumbnailsPerFrame)) {
        if (result.generation != imageGeneration_ || !result.image.ok) continue;
        std::string source = kImageSourcePrefix + std::to_string(imageGeneration_) + ":" +
                             std::to_string(imageSourceByPath_.size());
        imageBySource_[source] = std::move(result.image);
        imageSourceByPath_[result.pathUtf8] = std::move(source);
        imageAdded = true;
    }
    if (imageAdded) RebuildCards();

    if (!pendingFocus_) return;
    // data-ifで出した要素は次のレイアウトまで無いので、見つかるまで次のフレームで試す。
    Rml::Element* const element = document.GetElementById(kNameInputId);
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (input == nullptr) return;
    pendingFocus_ = false;
    input->SetValue(model_->name);
    input->Focus();
    input->Select();
}

bool RmlUiLayoutFilesView::ResolveImageSource(const std::string& source, rs::render::ImageData& out) const {
    const std::map<std::string, rs::render::ImageData>::const_iterator image = imageBySource_.find(source);
    if (image == imageBySource_.end()) return false;
    out = image->second;
    return true;
}

void RmlUiLayoutFilesView::ReleaseImages() {
    for (const std::pair<const std::string, rs::render::ImageData>& image : imageBySource_) {
        Rml::ReleaseTexture(image.first);
    }
    imageBySource_.clear();
    imageSourceByPath_.clear();
}

void RmlUiLayoutFilesView::OnSelectTab(const Rml::VariantList& arguments) {
    const Rml::String key = FirstEventArgument<Rml::String>(arguments, Rml::String());
    if (key != kTabOwn && key != kTabRs2) return;
    if (key == model_->tab) return;
    model_->tab = key;
    pendingScrollTop_ = true;
    if (modelHandle_) modelHandle_.DirtyVariable("layoutFilesTab");
}

void RmlUiLayoutFilesView::OnRs2Search(Rml::Event& event) {
    model_->rs2Search = event.GetParameter<Rml::String>("value", Rml::String());
    RebuildRs2Cards();
    if (modelHandle_) modelHandle_.DirtyVariable("layoutRs2Search");
}

void RmlUiLayoutFilesView::RebuildRs2Cards() {
    const std::vector<std::string> terms = rs::platform_text::SplitSearchTerms(model_->rs2Search);
    rs2Shown_.clear();
    model_->rs2Cards.clear();
    for (std::size_t i = 0; i < rs2_.size(); ++i) {
        if (!rs::platform_text::ContainsAllTerms(rs::platform_text::ToAsciiLower(rs2_[i].nameUtf8), terms)) continue;
        rs2Shown_.push_back(i);
        model_->rs2Cards.push_back({rs2_[i].nameUtf8, rs2_[i].dateUtf8, Rml::String()});
    }
    if (modelHandle_) {
        modelHandle_.DirtyVariable("layoutRs2Cards");
        modelHandle_.DirtyVariable("layoutRs2Available");
    }
}

void RmlUiLayoutFilesView::RebuildCards() {
    model_->recentCards = ToCards(recent_, imageSourceByPath_);
    model_->folderCards = ToCards(folder_, imageSourceByPath_);
    if (!modelHandle_) return;
    modelHandle_.DirtyVariable("layoutRecentCards");
    modelHandle_.DirtyVariable("layoutFolderCards");
}

void RmlUiLayoutFilesView::OnOpenOther() {
    picker_.StartOpenFile(ownerWindow_, kPickerTitle, rs::platform_file::kLayoutFileType); // 開いている間は何もしない(ボタンも無効)
}

std::optional<LayoutFilesCommand> RmlUiLayoutFilesView::TakeCommand() {
    std::optional<LayoutFilesCommand> command = std::move(command_);
    command_.reset();
    return command;
}

void RmlUiLayoutFilesView::OnCardClick(const std::vector<LayoutFileEntry>& entries, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size()) return;
    const LayoutFileEntry& entry = entries[static_cast<std::size_t>(index)];
    if (mode_ == LayoutFilesMode::Load) {
        command_ = LayoutFilesCommand{LayoutFilesCommand::Kind::LoadFile, entry.pathUtf8};
        return;
    }
    SetName(entry.nameUtf8);
}

void RmlUiLayoutFilesView::OnNameChange(Rml::Event& event) {
    const Rml::String value = event.GetParameter<Rml::String>("value", Rml::String());
    SetName(value);
    if (event.GetParameter<bool>("linebreak", false)) SubmitName();
}

void RmlUiLayoutFilesView::OnNameKey(Rml::Event& event) {
    // 入力欄にフォーカスがあるとEscは入力欄へ届くので、ここで画面を閉じる。
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    command_ = LayoutFilesCommand{LayoutFilesCommand::Kind::Close, {}};
}

void RmlUiLayoutFilesView::SetName(const std::string& name) {
    model_->name = name;
    model_->nameValid = IsValidLayoutFileName(name);
    if (!modelHandle_) return;
    modelHandle_.DirtyVariable("layoutFileName");
    modelHandle_.DirtyVariable("layoutFileNameValid");
}

void RmlUiLayoutFilesView::SubmitName() {
    if (!model_->nameValid) return;
    command_ = LayoutFilesCommand{LayoutFilesCommand::Kind::SaveName, model_->name};
}

} // namespace rs::app::rmlui

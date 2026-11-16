#include "RmlUiTrainTabView.h"

#include <optional>
#include <string>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "../action/TemplateAction.h"
#include "RmlUiUpdateVariable.h"
#include "../action/FormationAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_session/TemplateSaver.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app::rmlui {

namespace {
constexpr const char* kPageKey = "trainTabPage";
constexpr const char* kTemplatesOpenKey = "browserTemplatesOpen";
constexpr const char* kTemplatesKey = "browserTemplates";
constexpr const char* kTemplateTabVisibleKey = "templateTabVisible";
constexpr const char* kPageList = "list";
constexpr const char* kPageBrowser = "browser";
} // namespace

// テンプレートのカード1枚(データモデルの `browserTemplates` の要素)。
struct TemplateCardRow {
    Rml::String name;
    Rml::String image;  // RmlUiのテクスチャの名前。撮れるまで・撮らない種類は空
    Rml::String count;  // 「N両」。両数を出さない種類は空
    bool missing = false; // 見つからない車両がある
};

// データモデルへ出す値(定義はここだけ)。
struct TrainTabModel {
    Rml::String page = kPageList;
    bool templatesOpen = false;
    // テンプレートの分類のタブを出すか(レール・情景・列車のプロファイル)。
    bool templateTabVisible = false;
    Rml::Vector<TemplateCardRow> templates;
};

RmlUiTrainTabView::RmlUiTrainTabView(ToastQueue& queue) : queue_(queue), model_(std::make_unique<TrainTabModel>()) {}
RmlUiTrainTabView::~RmlUiTrainTabView() = default;

namespace {
// プロファイルごとに、テンプレートの名前を読む保存先の種類(分類のタブを出さないプロファイルは空)。
std::vector<TemplateKind> TemplateKindsFor(plugin_browser::BrowserProfile profile) {
    using plugin_browser::BrowserProfile;
    switch (profile) {
    case BrowserProfile::Rail: return {TemplateKind::RailwayPluginSet};
    // 情景のテンプレート(StationBuildTemplate・StructBuildTemplate)は置く操作が無いので、分類を出さない。
    case BrowserProfile::Train: return {TemplateKind::TrainGroupTemplate};
    default: return {};
    }
}
} // namespace

void RmlUiTrainTabView::Attach(const FormationRefs& refs) {
    refs_ = std::make_unique<FormationRefs>(refs);
}

void RmlUiTrainTabView::AttachLaying(rs::editor::Editor& editor, PluginSelector& selector,
                                     const std::vector<std::string>& pluginRoots) {
    editor_ = &editor;
    selector_ = &selector;
    pluginRoots_ = &pluginRoots;
}

void RmlUiTrainTabView::AttachBrowser(plugin_browser::PluginBrowserService& browser) {
    browser_ = &browser;
}

void RmlUiTrainTabView::Refresh(plugin_browser::BrowserProfile profile, const std::vector<std::string>& pluginRoots) {
    const std::string root = pluginRoots.empty() ? std::string() : pluginRoots.front();
    const bool visible = !TemplateKindsFor(profile).empty();
    UpdateVariable(modelHandle_, kTemplateTabVisibleKey, model_->templateTabVisible, visible);
    if (profile == profile_ && root == pluginRoot_) {
        UpdateTemplateImages();
        return;
    }
    // プロファイルが変わったら、開いていた分類は閉じる(別の種類のテンプレートを見せない)。
    if (profile != profile_) {
        SetTemplatesOpen(false);
        // 列車モードに入るたびに、編成一覧のページから始める。
        if (profile == plugin_browser::BrowserProfile::Train) SetPage(kPageList);
    }
    profile_ = profile;
    pluginRoot_ = root;
    ReloadTemplates();
    UpdateTemplateImages();
}

void RmlUiTrainTabView::ReloadTemplates() {
    Rml::Vector<TemplateCardRow> rows;
    std::vector<plugin_browser::WantedTemplateImage> requests;
    std::vector<std::size_t> requestRows;
    for (const TemplateKind kind : TemplateKindsFor(profile_)) {
        for (const std::string& name : ListTemplateNames(pluginRoot_, kind)) {
            TemplateCardRow row;
            row.name = name;
            // 編成・敷設は画像を撮る。両数と見つからない車両の警告は編成だけ(情景は名前だけ)。
            const std::string path = TemplateSavePath(pluginRoot_, kind, name).string();
            if (kind == TemplateKind::TrainGroupTemplate || kind == TemplateKind::RailwayPluginSet) {
                plugin_browser::WantedTemplateImage wanted;
                wanted.source.kind = kind == TemplateKind::TrainGroupTemplate ? plugin_browser::TemplateImageKind::Train
                                                                              : plugin_browser::TemplateImageKind::Rail;
                wanted.source.pathUtf8 = path;
                wanted.source.rootUtf8 = pluginRoot_;
                wanted.key = plugin_browser::MakeTemplateImageKey(wanted.source);
                requests.push_back(std::move(wanted));
                requestRows.push_back(rows.size());
            }
            if (kind == TemplateKind::TrainGroupTemplate) {
                const TrainGroupTemplateSummary summary =
                    SummarizeTrainGroupTemplate(ParseTrainGroupTemplateFile(path, pluginRoot_));
                row.count = std::to_string(summary.carCount) + "両";
                row.missing = summary.missingCount > 0;
            }
            rows.push_back(std::move(row));
        }
    }
    imageRequests_ = std::move(requests);
    imageRequestRows_ = std::move(requestRows);
    // 名前・両数・警告が前と同じ一覧でも、中身が書き換わるとキーが変わるので、毎回作り直して、撮れている画像を入れ直す。
    model_->templates = std::move(rows);
    ApplyTemplateImages();
    if (modelHandle_) modelHandle_.DirtyVariable(kTemplatesKey);
}

bool RmlUiTrainTabView::ApplyTemplateImages() {
    if (browser_ == nullptr) return false;
    const plugin_browser::TemplateImageStore& store = browser_->TemplateImages();
    bool changed = false;
    for (std::size_t i = 0; i < imageRequests_.size(); ++i) {
        const std::size_t row = imageRequestRows_[i];
        if (row >= model_->templates.size()) continue;
        const plugin_browser::TemplateImageKey key = imageRequests_[i].key;
        const Rml::String image =
            store.Find(key) != nullptr ? plugin_browser::MakeTemplateImageName(key) : Rml::String();
        if (model_->templates[row].image == image) continue;
        model_->templates[row].image = image;
        changed = true;
    }
    return changed;
}

void RmlUiTrainTabView::UpdateTemplateImages() {
    if (browser_ == nullptr || !model_->templateTabVisible || !model_->templatesOpen) return;
    browser_->TemplateImages().Request(imageRequests_, browser_->ImageWorker());
    if (ApplyTemplateImages() && modelHandle_) modelHandle_.DirtyVariable(kTemplatesKey);
}

void RmlUiTrainTabView::SetPage(const char* page) {
    UpdateVariable(modelHandle_, kPageKey, model_->page, Rml::String(page));
}

void RmlUiTrainTabView::SetTemplatesOpen(bool open) {
    UpdateVariable(modelHandle_, kTemplatesOpenKey, model_->templatesOpen, open);
}

void RmlUiTrainTabView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<TemplateCardRow> card = constructor.RegisterStruct<TemplateCardRow>();
    card.RegisterMember("name", &TemplateCardRow::name);
    card.RegisterMember("image", &TemplateCardRow::image);
    card.RegisterMember("count", &TemplateCardRow::count);
    card.RegisterMember("missing", &TemplateCardRow::missing);
    constructor.RegisterArray<Rml::Vector<TemplateCardRow>>();
    constructor.Bind(kPageKey, &model_->page);
    constructor.Bind(kTemplatesOpenKey, &model_->templatesOpen);
    constructor.Bind(kTemplatesKey, &model_->templates);
    constructor.Bind(kTemplateTabVisibleKey, &model_->templateTabVisible);

    // 一覧の編成カードのクリック。配置済みも含めて、その編成を選ぶだけ(ページは移らない)。
    constructor.BindEventCallback(
        "formation_card_click", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const int index = EventArgumentAt<int>(arguments, 0, -1);
            if (index >= 0) FormationAction(*refs_).SelectFormation(static_cast<std::size_t>(index));
        });
    // カードのダブルクリック。配置済みなら先頭車へカメラを移し(運転台視点には入らない)、
    // 未配置ならその編成を選んでブラウザのページへ移る(車両を足す編成編集パネルが出る)。
    constructor.BindEventCallback(
        "formation_card_edit", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const int index = EventArgumentAt<int>(arguments, 0, -1);
            const bool placed = EventArgumentAt<bool>(arguments, 1, false);
            const Rml::String cardId = EventArgumentAt<Rml::String>(arguments, 2, Rml::String());
            if (placed) {
                const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(cardId);
                if (guid.has_value()) FormationAction(*refs_).FollowFormation(*guid);
                return;
            }
            if (index < 0) return;
            FormationAction(*refs_).SelectFormation(static_cast<std::size_t>(index));
            SetTemplatesOpen(false);
            SetPage(kPageBrowser);
        });
    // 新規編成。作った編成が選ばれるので、車両を足せるようブラウザのページへ移る。
    constructor.BindEventCallback(
        "train_tab_add_formation", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (!refs_) return;
            FormationAction(*refs_).AddFormation();
            SetTemplatesOpen(false);
            SetPage(kPageBrowser);
        });
    // 編成編集パネルの末尾の`+`。選んでいる編成へ車両を足すため、ブラウザのページへ移る。
    constructor.BindEventCallback(
        "train_tab_open_browser", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            SetTemplatesOpen(false);
            SetPage(kPageBrowser);
        });
    constructor.BindEventCallback(
        "train_tab_back", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            SetTemplatesOpen(false);
            SetPage(kPageList);
        });
    constructor.BindEventCallback(
        "browser_templates_open", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
            event.StopPropagation(); // 種別タブの押下(分類を閉じる)へ伝えない
            ReloadTemplates(); // 保存したばかりのテンプレートも並べる
            SetTemplatesOpen(true);
        });
    // テンプレートのカードを押す。レールは敷設プラグインセットへ読み込む。列車はドラッグで使うので何も起きない。
    constructor.BindEventCallback(
        "template_card_click", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (profile_ == plugin_browser::BrowserProfile::Rail) {
                if (editor_ == nullptr || selector_ == nullptr || pluginRoots_ == nullptr) return;
                template_action::LoadLayingTemplate(queue_, {*editor_, *selector_, *pluginRoots_},
                                                    EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
            }
        });
    constructor.BindEventCallback(
        "browser_templates_close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            SetTemplatesOpen(false);
        });
    modelHandle_ = constructor.GetModelHandle();
}

} // namespace rs::app::rmlui

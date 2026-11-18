#include "RmlUiTrainTabView.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "../action/TemplateAction.h"
#include "RmlUiUpdateVariable.h"
#include "../action/FormationAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_browser/TemplateCardInfoWorker.h"
#include "../scene/ParsedPluginCache.h"
#include "../plugin_session/TemplateSaver.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "../../platform/text/Encoding.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app::rmlui {

namespace {
constexpr const char* kPageKey = "trainTabPage";
constexpr const char* kTemplatesOpenKey = "browserTemplatesOpen";
constexpr const char* kTemplatesKey = "browserTemplates";
constexpr const char* kTemplateTabVisibleKey = "templateTabVisible";
constexpr const char* kPageList = "list";
constexpr const char* kPageBrowser = "browser";

double ElapsedMs(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}
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
    // 画面に出す行。分類のタブが開いている間だけ入れる(閉じている間も要素を作ると、列車モードの出入りのたびに
    // 数百枚のカードを作って壊すので止まる)。
    Rml::Vector<TemplateCardRow> templates;
    // 両数・警告・画像を埋めていく作業用の行。画面には出さない。
    Rml::Vector<TemplateCardRow> cards;
};

struct RmlUiTrainTabView::ListRequest {
    std::uint64_t round = 0;
    std::string root;
    std::vector<TemplateKind> kinds;
};
struct RmlUiTrainTabView::ListResult {
    std::uint64_t round = 0;
    std::vector<std::pair<TemplateKind, std::vector<std::string>>> lists;
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
    // プラグインの読込の世代が変わると、求めた画像のキーが古くなるので、一覧を読み直して頼み直す。
    if (profile == profile_ && root == pluginRoot_ &&
        (listPending_ || GetParsedPluginCache().Generation() == infoGeneration_)) {
        PollTemplateList();
        PollTemplateCardInfo();
        UpdateTemplateImages();
        SyncShownTemplates();
        return;
    }
    // プロファイルが変わったら、開いていた分類は閉じる(別の種類のテンプレートを見せない)。
    if (profile != profile_) {
        SetTemplatesOpen(false);
        // 列車モードに入るたびに、編成一覧のページから始める。
        if (profile == plugin_browser::BrowserProfile::Train) SetPage(kPageList);
        model_->cards.clear(); // 別の種類の行の両数・画像を、同じ名前へ引き継がない
        cardsChanged_ = true;
    }
    profile_ = profile;
    pluginRoot_ = root;
    ReloadTemplates();
    PollTemplateList();
    UpdateTemplateImages();
    SyncShownTemplates();
}

void RmlUiTrainTabView::ReloadTemplates() {
    if (!listWorker_) {
        listWorker_ = std::make_unique<rs::platform_thread::WorkerThread<ListRequest, ListResult>>(
            [](const ListRequest& request, std::stop_token) {
                ListResult result;
                result.round = request.round;
                for (const TemplateKind kind : request.kinds) {
                    result.lists.emplace_back(kind, ListTemplateNames(request.root, kind));
                }
                return result;
            });
    }
    listWorker_->ClearPending(); // 古い頼みは捨てる(プロファイルの切り替えを繰り返しても溜めない)
    ListRequest request;
    request.round = ++listRound_;
    request.root = pluginRoot_;
    request.kinds = TemplateKindsFor(profile_);
    listWorker_->Post(std::move(request), 0);
    listPending_ = true;
}

void RmlUiTrainTabView::PollTemplateList() {
    if (!listWorker_ || !listPending_) return;
    std::vector<ListResult> results = listWorker_->Drain(8);
    // 今の回の結果だけ使う(古い回の結果は捨てる)。
    const std::vector<ListResult>::iterator current =
        std::find_if(results.begin(), results.end(), [this](const ListResult& r) { return r.round == listRound_; });
    if (current == results.end()) return;
    listPending_ = false;
    const ListResult& listed = *current;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point tStart = Clock::now();
    std::size_t templateCount = 0;
    // 同じ名前の行は、新しい結果が届くまで前の両数・警告・画像を出しておく(開くたびに空白にならないように)。
    std::unordered_map<std::string, std::size_t> previousRows;
    for (std::size_t i = 0; i < model_->cards.size(); ++i) previousRows[model_->cards[i].name] = i;
    const Rml::Vector<TemplateCardRow> previous = model_->cards;

    Rml::Vector<TemplateCardRow> rows;
    std::vector<std::optional<plugin_browser::TemplateImageSource>> sources;
    for (const std::pair<TemplateKind, std::vector<std::string>>& list : listed.lists) {
        const TemplateKind kind = list.first;
        const std::vector<std::string>& names = list.second;
        templateCount += names.size();
        for (const std::string& name : names) {
            const std::unordered_map<std::string, std::size_t>::const_iterator old = previousRows.find(name);
            TemplateCardRow row = old != previousRows.end() ? previous[old->second] : TemplateCardRow();
            row.name = name;
            // 編成・敷設は画像を撮る。両数と見つからない車両の警告は編成だけ(情景は名前だけ)。
            if (kind == TemplateKind::TrainGroupTemplate || kind == TemplateKind::RailwayPluginSet) {
                plugin_browser::TemplateImageSource source;
                source.kind = kind == TemplateKind::TrainGroupTemplate ? plugin_browser::TemplateImageKind::Train
                                                                       : plugin_browser::TemplateImageKind::Rail;
                // path::string() はANSIコードページで返すので、日本語の名前がUTF-8として読めなくなる。
                source.pathUtf8 = rs::platform_text::WideToUtf8(TemplateSavePath(pluginRoot_, kind, name).wstring());
                source.rootUtf8 = pluginRoot_;
                sources.push_back(std::move(source));
            } else {
                sources.push_back(std::nullopt);
            }
            rows.push_back(std::move(row));
        }
    }
    // 前の回の結果は捨て、画像の要求は、キーが届いた行から作る。
    rowSources_ = std::move(sources);
    rowWanted_.assign(rowSources_.size(), std::nullopt);
    imageRequests_.clear();
    imageRequestRows_.clear();
    model_->cards = std::move(rows);
    cardsChanged_ = true;

    const Clock::time_point tPost = Clock::now();
    if (!infoWorker_) infoWorker_ = std::make_unique<plugin_browser::TemplateCardInfoWorker>();
    infoGeneration_ = GetParsedPluginCache().Generation();
    const std::uint64_t round = infoWorker_->Begin();
    for (std::size_t row = 0; row < rowSources_.size(); ++row) {
        if (rowSources_[row].has_value()) infoWorker_->Post(round, row, *rowSources_[row], infoGeneration_);
    }
    const double postMs = ElapsedMs(tPost);
    const double totalMs = ElapsedMs(tStart);
    if (totalMs >= 100.0) {
        spdlog::warn("[slow] PollTemplateList(描画スレッド) {:.0f}ms 件数={} ワーカーへの依頼={:.0f}ms",
                     totalMs, templateCount, postMs);
    }
}

bool RmlUiTrainTabView::PollTemplateCardInfo() {
    if (!infoWorker_) return false;
    const std::chrono::steady_clock::time_point tStart = std::chrono::steady_clock::now();
    const std::vector<plugin_browser::TemplateCardInfoResult> results = infoWorker_->Drain(infoGeneration_);
    if (results.empty()) return false;
    for (const plugin_browser::TemplateCardInfoResult& result : results) {
        if (result.row >= model_->cards.size() || !rowSources_[result.row].has_value()) continue;
        TemplateCardRow& card = model_->cards[result.row];
        if (result.info.hasSummary) {
            card.count = std::to_string(result.info.carCount) + "両";
            card.missing = result.info.missingCount > 0;
        }
        plugin_browser::WantedTemplateImage wanted;
        wanted.key = result.info.imageKey;
        wanted.source = *rowSources_[result.row];
        rowWanted_[result.row] = std::move(wanted);
    }
    RebuildImageRequests();
    ApplyTemplateImages();
    cardsChanged_ = true;
    const double ms = ElapsedMs(tStart);
    if (ms >= 50.0) spdlog::warn("[slow] PollTemplateCardInfo(描画スレッド) {:.0f}ms 結果={}件", ms, results.size());
    return true;
}

void RmlUiTrainTabView::RebuildImageRequests() {
    imageRequests_.clear();
    imageRequestRows_.clear();
    for (std::size_t row = 0; row < rowWanted_.size(); ++row) {
        if (!rowWanted_[row].has_value()) continue;
        imageRequests_.push_back(*rowWanted_[row]);
        imageRequestRows_.push_back(row);
    }
}

bool RmlUiTrainTabView::ApplyTemplateImages() {
    if (browser_ == nullptr) return false;
    const plugin_browser::TemplateImageStore& store = browser_->TemplateImages();
    bool changed = false;
    for (std::size_t i = 0; i < imageRequests_.size(); ++i) {
        const std::size_t row = imageRequestRows_[i];
        if (row >= model_->cards.size()) continue;
        const plugin_browser::TemplateImageKey key = imageRequests_[i].key;
        const Rml::String image =
            store.Find(key) != nullptr ? plugin_browser::MakeTemplateImageName(key) : Rml::String();
        if (model_->cards[row].image == image) continue;
        model_->cards[row].image = image;
        changed = true;
    }
    return changed;
}

void RmlUiTrainTabView::UpdateTemplateImages() {
    if (browser_ == nullptr || !model_->templateTabVisible || !model_->templatesOpen) return;
    browser_->TemplateImages().Request(imageRequests_, browser_->ImageWorker());
    if (ApplyTemplateImages()) cardsChanged_ = true;
}

void RmlUiTrainTabView::SyncShownTemplates() {
    const bool shown = model_->templateTabVisible && model_->templatesOpen;
    if (!shown) {
        if (model_->templates.empty()) return;
        model_->templates.clear();
    } else {
        // 開いた直後は、作業用の行が変わっていなくても行数が違うので写す。
        if (!cardsChanged_ && model_->templates.size() == model_->cards.size()) return;
        model_->templates = model_->cards;
    }
    cardsChanged_ = false;
    if (modelHandle_) modelHandle_.DirtyVariable(kTemplatesKey);
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

#include "RmlUiPluginBrowserView.h"

#include <optional>
#include <format>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Traits.h>

#include <spdlog/spdlog.h>

#include "RmlUiBrowserGridDriver.h"
#include "RmlUiBrowserHoverText.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdatePhase.h"
#include "RmlUiPluginBrowserInternal.h"
#include "RmlUiPluginBrowserModel.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiNumberParse.h"
#include "../action/PluginBrowserCardSizeAction.h"
#include "../action/PluginFavoriteAction.h"
#include "../action/SceneryTransformValues.h"
#include "../action/SceneSignalAspectSource.h"
#include "../action/SceneSignalSidecarSource.h"
#include "../action/SceneryPlacementAction.h"
#include "../action/SignalBlockAction.h"
#include "../action/TrackPluginSetAction.h"
#include "../action/TrackPluginSetText.h"
#include "../action/TrackLayingAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_session/PluginSelector.h"
#include "../settings/UserSettings.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app::rmlui {

using rs::app::plugin_browser::BrowserCardAction;
using rs::app::plugin_browser::BrowserProfile;
using rs::app::plugin_browser::BrowserProfileTraits;
using rs::app::plugin_browser::BrowserTab;
using rs::app::plugin_browser::EntryId;
using rs::app::plugin_browser::PluginBrowserService;
using rs::app::plugin_browser::PluginBrowserState;
using rs::app::plugin_browser::PluginCatalogEntry;

namespace {

// 登録(RegisterModel)と更新(Refresh*)の両方で使うバインド名。
constexpr const char* kTabsKey = "browserTabs";
constexpr const char* kSearchKey = "browserSearch";
constexpr const char* kSortByAuthorKey = "browserSortByAuthor";
constexpr const char* kSortDescendingKey = "browserSortDescending";
constexpr const char* kFilterOptionsKey = "browserFilterOptions";
constexpr const char* kFilterPanelOpenKey = "browserFilterPanelOpen";
constexpr const char* kFilterActiveKey = "browserFilterActive";
constexpr const char* kCardsKey = "browserCards";
constexpr const char* kTopSpaceKey = "browserTopSpace";
constexpr const char* kBottomSpaceKey = "browserBottomSpace";
constexpr const char* kStatusKey = "browserStatus";
constexpr const char* kScanningKey = "browserScanning";
constexpr const char* kSetButtonsKey = "browserSetButtons";
constexpr const char* kPlacementOptionsKey = "browserPlacementOptions";
constexpr const char* kSceneryContinuousKey = "sceneryContinuous";
constexpr const char* kScenerySnapToRailKey = "scenerySnapToRail";
constexpr const char* kSceneryTransformShownKey = "sceneryTransformShown";
constexpr const char* kDetailKey = "browserDetail";
constexpr const char* kCardSizeKey = "browserCardSize";
constexpr const char* kCardWidthKey = "browserCardWidth";
constexpr const char* kThumbHeightKey = "browserThumbHeight";
constexpr const char* kNameFontSizeKey = "browserCardNameFontSize";
constexpr const char* kAuthorFontSizeKey = "browserCardAuthorFontSize";
constexpr const char* kHoverNameKey = "browserHoverName";
constexpr const char* kHoverDetailKey = "browserHoverDetail";
constexpr const char* kSelectedNameKey = "browserSelectedName";

// 状態の文言(仮。ローカライズ基盤より前)。何も出せないときは、理由ごとに別の文を出す。
} // namespace

RmlUiPluginBrowserView::RmlUiPluginBrowserView() : model_(std::make_unique<PluginBrowserModel>()) {}
RmlUiPluginBrowserView::~RmlUiPluginBrowserView() = default;

void RmlUiPluginBrowserView::Attach(PluginBrowserService& service, rs::editor::Editor& editor,
                                    PluginSelector& selector, UserSettingsData& userSettings,
                                    SceneRenderer& sceneRenderer, rs::world::SceneRegistry& sceneRegistry,
                                    bool& sceneryContinuous, bool& scenerySnapToRail,
                                    const rs::editor::PlacePreview& preview, ToastQueue& queue) {
    continuousPlacement_ = &sceneryContinuous;
    snapToRail_ = &scenerySnapToRail;
    service_ = &service;
    sceneRenderer_ = &sceneRenderer;
    sceneRegistry_ = &sceneRegistry;
    editor_ = &editor;
    preview_ = &preview;
    selector_ = &selector;
    userSettings_ = &userSettings;
    folder_.Attach(service.State(), queue);
}

void RmlUiPluginBrowserView::RegisterModel(Rml::DataModelConstructor& constructor) {
    RegisterBrowserModelTypes(constructor);
    constructor.Bind(kTabsKey, &model_->tabs);
    constructor.Bind(kSearchKey, &model_->search);
    constructor.Bind(kSortByAuthorKey, &model_->sortByAuthor);
    constructor.Bind(kSortDescendingKey, &model_->sortDescending);
    for (const plugin_browser::BrowserFilterTraits& filter : plugin_browser::kBrowserFilters) {
        model_->filterOptions.push_back({filter.key, filter.label, false, !filter.needsSetProfile});
    }
    constructor.Bind(kFilterOptionsKey, &model_->filterOptions);
    constructor.Bind(kFilterPanelOpenKey, &model_->filterPanelOpen);
    constructor.Bind(kFilterActiveKey, &model_->filterActive);
    constructor.Bind(kCardsKey, &model_->cards);
    constructor.Bind(kTopSpaceKey, &model_->topSpace);
    constructor.Bind(kBottomSpaceKey, &model_->bottomSpace);
    constructor.Bind(kStatusKey, &model_->status);
    constructor.Bind(kScanningKey, &model_->scanning);
    constructor.Bind(kSetButtonsKey, &model_->setButtons);
    constructor.Bind(kPlacementOptionsKey, &model_->placementOptions);
    constructor.Bind(kSceneryContinuousKey, &model_->continuousShown);
    constructor.Bind(kScenerySnapToRailKey, &model_->scenerySnapToRail);
    constructor.Bind(kSceneryTransformShownKey, &model_->sceneryTransformShown);
    constructor.Bind(kSceneryXKey, &model_->sceneryX);
    constructor.Bind(kSceneryYKey, &model_->sceneryY);
    constructor.Bind(kSceneryZKey, &model_->sceneryZ);
    constructor.Bind(kSceneryYawKey, &model_->sceneryYaw);
    constructor.Bind(kSceneryPitchKey, &model_->sceneryPitch);
    constructor.Bind(kSceneryRollKey, &model_->sceneryRoll);
    constructor.Bind(kDetailKey, &model_->detail);
    constructor.Bind(kCardSizeKey, &model_->cardSize);
    constructor.Bind(kCardWidthKey, &model_->cardWidth);
    constructor.Bind(kThumbHeightKey, &model_->thumbHeight);
    constructor.Bind(kNameFontSizeKey, &model_->nameFontSize);
    constructor.Bind(kAuthorFontSizeKey, &model_->authorFontSize);
    constructor.Bind(kHoverNameKey, &model_->hoverName);
    constructor.Bind(kHoverDetailKey, &model_->hoverDetail);
    constructor.Bind(kSelectedNameKey, &model_->selectedName);

    // 階層が変わったら先頭から見せ直す(ホバーも外す)。
    folder_.Register(constructor, *model_, [this]() { scrollToTopPending_ = true; hoveredEntry_.reset(); });
    constructor.BindEventCallback("select_browser_tab",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSelectTab(arguments);
                                  });
    constructor.BindEventCallback("browser_search",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnSearch(event);
                                  });
    constructor.BindEventCallback("set_browser_sort_field",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSortAxis(arguments, true);
                                  });
    constructor.BindEventCallback("set_browser_sort_direction",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSortAxis(arguments, false);
                                  });
    constructor.BindEventCallback("toggle_browser_filter",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      // チェックボックスのchangeは、データ結線がcheckedを書き換えたときにも届く(解除ボタンなど)。
                                      if (IsInsideContextUpdate()) return;
                                      OnToggleFilter(arguments);
                                  });
    constructor.BindEventCallback("clear_browser_filters",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      OnClearFilters();
                                  });
    // 漏斗のパネルの開閉(外側のクリックとEscで閉じるのはRmlUiIntegration側)。
    constructor.BindEventCallback("toggle_browser_filter_panel",
                                  [this](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
                                      model_->filterPanelOpen = !model_->filterPanelOpen;
                                      handle.DirtyVariable(kFilterPanelOpenKey);
                                  });
    constructor.BindEventCallback("toggle_favorite",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnToggleFavorite(event, arguments);
                                  });
    constructor.BindEventCallback("toggle_set",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnToggleSet(event, arguments);
                                  });
    constructor.BindEventCallback("place_plugin",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnPlace(arguments);
                                  });
    constructor.BindEventCallback("show_duplicate",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnShowDuplicate(event, arguments);
                                  });
    constructor.BindEventCallback("select_browser_card_size",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnSelectCardSize(arguments);
                                  });
    // カードにマウスを乗せている間だけ詳細の行へ名前・作者・説明文を出す(出入りでカードのidを覚える)。
    constructor.BindEventCallback("hover_browser_card",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      OnHoverCard(arguments);
                                  });
    constructor.BindEventCallback("unhover_browser_card",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { hoveredEntry_.reset(); });
    // 配置設定のトグル。チェックボックスの今の状態(checked属性)を変数へ写す(表示はすでに合っているので
    // Dirtyにしない)。
    constructor.BindEventCallback("toggle_scenery_continuous",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      const Rml::Element* element = event.GetCurrentElement();
                                      if (element == nullptr || continuousPlacement_ == nullptr) return;
                                      *continuousPlacement_ = element->HasAttribute("checked");
                                      model_->continuousShown = *continuousPlacement_;
                                  });
    constructor.BindEventCallback("toggle_scenery_snap_to_rail",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      const Rml::Element* element = event.GetCurrentElement();
                                      if (element == nullptr || snapToRail_ == nullptr) return;
                                      *snapToRail_ = element->HasAttribute("checked");
                                      model_->scenerySnapToRail = *snapToRail_;
                                  });
    constructor.BindEventCallback("commit_scenery_transform",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
                                      OnCommitSceneryTransform(event, arguments);
                                  });
    constructor.BindEventCallback("focus_scenery_transform",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      sceneryTransformInputFocused_ = true;
                                  });

    modelHandle_ = constructor.GetModelHandle();
}

bool RmlUiPluginBrowserView::CloseFilterPanel() {
    if (!model_->filterPanelOpen) return false;
    model_->filterPanelOpen = false;
    if (modelHandle_) modelHandle_.DirtyVariable(kFilterPanelOpenKey);
    return true;
}

void RmlUiPluginBrowserView::Refresh(Rml::ElementDocument& document) {
    if (service_ == nullptr || !modelHandle_) return;
    PluginBrowserState& state = service_->State();
    const BrowserProfileTraits& traits = plugin_browser::GetBrowserProfile(state.Profile());
    UpdateVariable(modelHandle_, kSetButtonsKey, model_->setButtons, traits.cardAction == BrowserCardAction::ToggleSet);
    const bool hasTransformSelection = traits.profile == BrowserProfile::Scenery && editor_->HasSelectedSceneEntity();
    UpdateVariable(modelHandle_, kPlacementOptionsKey, model_->placementOptions,
                   traits.showPlacementOptions && !hasTransformSelection);
    UpdateVariable(modelHandle_, kSceneryTransformShownKey, model_->sceneryTransformShown, hasTransformSelection);
    RefreshSceneryTransform();

    if (continuousPlacement_ != nullptr) {
        UpdateVariable(modelHandle_, kSceneryContinuousKey, model_->continuousShown, *continuousPlacement_);
    }
    if (snapToRail_ != nullptr) {
        UpdateVariable(modelHandle_, kScenerySnapToRailKey, model_->scenerySnapToRail, *snapToRail_);
    }

    selected_.Update(service_->Catalog(), *editor_, traits.selectedFolder);
    UpdateVariable(modelHandle_, kSelectedNameKey, model_->selectedName, Rml::String(selected_.Name()));

    // セットに入っているかは、セットのfolder列が変わったときだけ計算し直す(レールのプロファイルだけ)。
    // 絞り込みが同じ覚え書きを使うので、絞り込みの前に更新する。
    const bool tracksSet = traits.cardAction == BrowserCardAction::ToggleSet;
    plugin_browser::BrowserFilterContext filterContext;
    if (tracksSet) {
        std::vector<std::string> folders;
        for (const TrackPluginSetItem& item : TrackPluginSetAction(*editor_, *selector_).ViewModel().items) {
            folders.push_back(item.folderUtf8);
        }
        membership_.SetFolders(folders);
        filterContext.inSet = [this](EntryId id, const PluginCatalogEntry& entry) {
            return membership_.Contains(id, entry);
        };
        filterContext.setRevision = membership_.Revision();
    }
    state.Refilter(service_->Catalog(), PluginFavoriteAction(*userSettings_).Favorites(), service_->Trees(),
                   service_->TreesRevision(), filterContext);
    RefreshSortAndFilter(tracksSet);
    RefreshTabs();
    RefreshCards(document);
    RefreshDetail();
    RefreshHover();
    UpdateVariable(modelHandle_, kCardSizeKey, model_->cardSize,
                   Rml::String(plugin_browser::GetBrowserCardSize(PluginBrowserCardSizeAction(*userSettings_).Size()).key));
    RefreshStatus();
}

void RmlUiPluginBrowserView::OnSelectTab(const Rml::VariantList& arguments) {
    if (service_ == nullptr) return;
    const std::optional<BrowserTab> tab = ParseBrowserTabKey(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    if (!tab.has_value()) {
        spdlog::warn("[rmlui] select_browser_tabの引数が未知です");
        return;
    }
    operationError_.clear();
    service_->State().SelectTab(*tab);
    scrollToTopPending_ = true;
    hoveredEntry_.reset();
}

void RmlUiPluginBrowserView::OnSearch(Rml::Event& event) {
    if (service_ == nullptr) return;
    const Rml::String value = event.GetParameter<Rml::String>("value", Rml::String());
    model_->search = value; // 入力欄はすでに同じ文字を見せているので、Dirtyにはしない
    service_->State().SetSearch(value);
    scrollToTopPending_ = true;
    hoveredEntry_.reset();
}

void RmlUiPluginBrowserView::OnSortAxis(const Rml::VariantList& arguments, bool isField) {
    if (service_ == nullptr) return;
    // 引数は1か0(フィールド: 1=作者 / 向き: 1=降順)。もう一方の軸は今の値を引き継ぐ。
    const bool value = FirstEventArgument<int>(arguments, 0) != 0;
    const plugin_browser::BrowserSortTraits& now = plugin_browser::GetBrowserSort(service_->State().SortOrder());
    service_->State().SetSortOrder(isField ? plugin_browser::FindBrowserSort(value, now.descending)
                                           : plugin_browser::FindBrowserSort(now.byAuthor, value));
    scrollToTopPending_ = true;
    hoveredEntry_.reset();
}

void RmlUiPluginBrowserView::OnToggleFilter(const Rml::VariantList& arguments) {
    if (service_ == nullptr) return;
    const std::optional<plugin_browser::BrowserFilter> filter =
        plugin_browser::FindBrowserFilterByKey(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    if (!filter.has_value()) {
        spdlog::warn("[rmlui] toggle_browser_filterの引数が未知です");
        return;
    }
    service_->State().ToggleFilter(*filter);
    scrollToTopPending_ = true;
    hoveredEntry_.reset();
}

void RmlUiPluginBrowserView::OnClearFilters() {
    if (service_ == nullptr) return;
    service_->State().ClearFilters();
    scrollToTopPending_ = true;
    hoveredEntry_.reset();
}

void RmlUiPluginBrowserView::OnToggleFavorite(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation(); // カード本体の配置へ伝えない
    if (service_ == nullptr) return;
    const std::optional<EntryId> id = ReadEntryId(arguments, *service_);
    if (!id.has_value()) return;
    operationError_.clear();
    PluginFavoriteAction(*userSettings_).Toggle(service_->Catalog().Entries()[*id].relativeKey);
}

void RmlUiPluginBrowserView::OnToggleSet(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    if (service_ == nullptr) return;
    const std::optional<EntryId> id = ReadEntryId(arguments, *service_);
    if (!id.has_value()) return;
    ToggleSetMembership(*id);
}

void RmlUiPluginBrowserView::ToggleSetMembership(plugin_browser::EntryId id) {
    const PluginCatalogEntry& entry = service_->Catalog().Entries()[id];
    operationError_.clear();
    TrackPluginSetAction action(*editor_, *selector_);
    const TrackPluginSetResult result = action.Contains(entry.folderUtf8) ? action.Remove(entry.folderUtf8)
                                                                          : action.Add(entry.folderUtf8);
    operationError_ = TrackPluginSetErrorText(result);
}

void RmlUiPluginBrowserView::OnShowDuplicate(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    if (service_ == nullptr) return;
    const std::optional<EntryId> id = ReadEntryId(arguments, *service_);
    if (!id.has_value()) return;
    const std::string& key = service_->Catalog().Entries()[*id].relativeKey;
    // 同じカードでもう一度押すと消す。
    detailRelativeKey_ = (detailRelativeKey_ == key) ? std::string() : key;
}

void RmlUiPluginBrowserView::OnSelectCardSize(const Rml::VariantList& arguments) {
    if (userSettings_ == nullptr) return;
    const std::optional<plugin_browser::BrowserCardSize> size =
        plugin_browser::FindBrowserCardSizeByKey(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    if (!size.has_value()) {
        spdlog::warn("[rmlui] select_browser_card_sizeの引数が未知です");
        return;
    }
    // 大きさが変わると列数も行の高さも変わるので、先頭へ戻す(測り直しはカード幅の判定が受け持つ)。
    if (PluginBrowserCardSizeAction(*userSettings_).Set(*size)) {
        scrollToTopPending_ = true;
        hoveredEntry_.reset();
    }
}

void RmlUiPluginBrowserView::OnHoverCard(const Rml::VariantList& arguments) {
    if (service_ == nullptr) return;
    hoveredEntry_ = ReadEntryId(arguments, *service_);
}

} // namespace rs::app::rmlui

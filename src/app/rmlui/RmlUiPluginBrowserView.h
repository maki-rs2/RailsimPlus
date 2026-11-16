//
// RmlUiPluginBrowserView.h - プラグインブラウザ(左タブの一覧・タブ・検索・お気に入り・カードの操作・
// 情景の配置設定)をデータモデル"workspace"へ結ぶ。
//
// RmlUiLayingEditorViewと同じ形で、RmlUiWorkspaceViewが
// メンバとして持ち、CreateDataModelの中でRegisterModelを呼んで同じデータモデルへ変数とイベントを足す。
// 画面の状態(タブ・検索・絞り込み・窓の範囲の計算)はRmlUi非依存のPluginBrowserState側にあり、
// ここは結線だけを受け持つ。操作はActionを通す(ImGui版と共用)。
//
// DOMについて: 変数の反映はデータバインディングで行い、DOMへは書き込まない。Refreshが
// DOMから読むのは、グリッド(id="browser-grid")のスクロール位置・見えている高さと、描かれた
// カードの位置・外形だけ(行の高さと1行のカード数の実測。値の読み取りだけで、クラスの付け外しや
// 要素の探索結果の保持はしない)。唯一の書き込みは、タブや検索語が変わったときにグリッドを
// 先頭へ戻すスクロール位置(SetScrollTop(0))で、これはデータバインディングでは表せない。
//
// 毎フレームの更新(Refresh)は、前回データモデルへ入れた値と比べて、変わった変数だけ
// DirtyVariableする(DirtyAllVariablesは呼ばない)。
//
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../plugin_browser/PluginSetMembership.h"
#include "RmlUiBrowserFolder.h"
#include "RmlUiBrowserGridDriver.h"
#include "RmlUiBrowserSelected.h"
#include "RmlUiPluginBrowserImages.h"

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::editor {
class Editor;
struct PlacePreview;
} // namespace rs::editor

namespace rs::app {
class PluginSelector;
class SceneRenderer;
class ToastQueue;
struct UserSettingsData;
} // namespace rs::app

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser
namespace rs::world {
class SceneRegistry;
} // namespace rs::world

namespace rs::app::rmlui {

struct PluginBrowserModel;

class RmlUiPluginBrowserView {
public:
    RmlUiPluginBrowserView();
    ~RmlUiPluginBrowserView();
    RmlUiPluginBrowserView(const RmlUiPluginBrowserView&) = delete;
    RmlUiPluginBrowserView& operator=(const RmlUiPluginBrowserView&) = delete;

    // ブラウザの裏側と、操作の対象を渡す。どれも呼び出し側が所有し、このビューより長く生きること。
    void Attach(plugin_browser::PluginBrowserService& service, rs::editor::Editor& editor,
                PluginSelector& selector, UserSettingsData& userSettings, SceneRenderer& sceneRenderer,
                rs::world::SceneRegistry& sceneRegistry, bool& sceneryContinuous, bool& scenerySnapToRail,
                const rs::editor::PlacePreview& preview, ToastQueue& queue);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 漏斗のパネルを開いていれば閉じてtrueを返す(Escの消費の判断に使う)。
    bool CloseFilterPanel();

    // 左タブの中身がブラウザのフレームに毎回呼ぶ(PluginBrowserService::Tickの後)。Attach前は何もしない。
    void Refresh(Rml::ElementDocument& document);

    // 前のフレームのテクスチャ作成の合計時間(ms)と枚数。画面が見えていないフレームも含め毎フレーム渡す。
    void ObserveTextureLoads(double totalMs, std::size_t count) { images_.ObserveTextureLoads(totalMs, count); }

private:
    void RefreshSortAndFilter(bool hasSetProfile);
    void RefreshTabs();
    void RefreshCards(Rml::ElementDocument& document);
    void RefreshDetail();
    void RefreshHover();
    void RefreshStatus();

    void OnSelectTab(const Rml::VariantList& arguments);
    // 開いている種別に合わせて、配置物の追加/移動モードと架線の接続/切断モードを切り替える。
    void EnterTrackPlacementMode(std::optional<plugin_browser::BrowserKind> kind);
    void ApplyTabPlacementMode(const plugin_browser::BrowserTab& tab);
    void OnSearch(Rml::Event& event);
    // 並べ替えの軸を1つ変える(isField=trueは何で、偽は向き)。
    void OnSortAxis(const Rml::VariantList& arguments, bool isField);
    void OnToggleFilter(const Rml::VariantList& arguments);
    void OnClearFilters();
    void OnToggleFavorite(Rml::Event& event, const Rml::VariantList& arguments);
    void OnToggleSet(Rml::Event& event, const Rml::VariantList& arguments);
    // 敷設プラグインセットへ足す・外す(カードの`+`のボタンと、レール・枕木・桁のカード本体で共通)。
    void ToggleSetMembership(plugin_browser::EntryId id);
    void OnPlace(const Rml::VariantList& arguments);
    void OnShowDuplicate(Rml::Event& event, const Rml::VariantList& arguments);
    void OnSelectCardSize(const Rml::VariantList& arguments);
    void OnHoverCard(const Rml::VariantList& arguments);
    void OnCommitSceneryTransform(Rml::Event& event, const Rml::VariantList& arguments);
    void RefreshSceneryTransform();

    plugin_browser::PluginBrowserService* service_ = nullptr;
    rs::editor::Editor* editor_ = nullptr;
    const rs::editor::PlacePreview* preview_ = nullptr;
    PluginSelector* selector_ = nullptr;
    UserSettingsData* userSettings_ = nullptr;
    // 情景配置の連続配置の持ち主(ImGui版と同じSceneryPlacementUiState::continuousPlacement)。参照だけを持つ。
    bool* continuousPlacement_ = nullptr;
    bool* snapToRail_ = nullptr;
    SceneRenderer* sceneRenderer_ = nullptr; // 信号プラグインの速度表(station.json)の取得口
    rs::world::SceneRegistry* sceneRegistry_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<PluginBrowserModel> model_;
    // レールのセットに入っているかの覚え書き(カードごとに毎フレームSamePluginPathを呼ばないため)。
    plugin_browser::PluginSetMembership membership_;
    // 最上段に出す選択中のプラグイン(配置プレビュー中のもの)。
    RmlUiBrowserSelected selected_;
    // カードの画像のテクスチャ(要求・ソース名・解放)。
    RmlUiPluginBrowserImages images_;
    // グリッドの測定・カードの幅・行ごとの高さ・スクロールの補正・窓の範囲。
    RmlUiBrowserGridDriver gridDriver_;
    // フォルダのカードとパンくず。
    RmlUiBrowserFolder folder_;
    // マウスを乗せているカード(乗せていなければ空)。
    std::optional<plugin_browser::EntryId> hoveredEntry_;
    // 次のRefreshでグリッドを先頭へ戻す(タブ・検索語の切り替え直後)。
    bool scrollToTopPending_ = false;
    bool sceneryTransformInputFocused_ = false;
    // 重複の詳細を出しているカードのrelativeKey(出していなければ空)。
    std::string detailRelativeKey_;
    // 直前の操作の失敗の理由(次の操作かタブの切り替えで消す)。
    std::string operationError_;
};

} // namespace rs::app::rmlui

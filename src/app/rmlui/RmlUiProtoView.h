//
// RmlUiProtoView.h - 確認画面(proto.rml)のデータモデル"proto"の構築と結線。
//
// 確認画面のうち、選択の付け替えが
// 要る部品(テーマ切替の帯と、状態見本の先頭のセグメント)を、ワークスペースと同じく
// データバインディング(data-class-selected / data-event-click)で動かす。
// RmlUiWorkspaceViewと同じ書き方: モデルはContext::LoadDocument()より前に作り、
// 操作のあとにDirtyVariable()で表示へ反映する。
//
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../workspace/ContextMenuCatalog.h"
#include "RmlUiTheme.h"

namespace Rml {
class Context;
class Event;
} // namespace Rml

namespace rs::app::rmlui {

class RmlUiConfirmDialog;
class RmlUiContextMenu;

// コンテキストメニュー確認(ProtoDemo)の提供者も兼ねる。
class RmlUiProtoView : public workspace::IContextMenuProvider {
public:
    // themeSetはテーマ切替で使う配色一式。このビューより長く生きること
    // (RmlUiIntegrationのメンバを渡す)。
    explicit RmlUiProtoView(const UiThemeSet& themeSet);
    RmlUiProtoView(const RmlUiProtoView&) = delete;
    RmlUiProtoView& operator=(const RmlUiProtoView&) = delete;

    // Rml::Context::LoadDocument()より前に呼ぶこと(data-modelは文書の読込時に解決される)。
    // 失敗したらfalseを返す。
    bool CreateDataModel(Rml::Context& context);

    // ProtoDemoの提供者としてメニューへ登録し、確認ダイアログの確認画面で使うダイアログを受け取る。
    // dialogはこのビューより長く生きること。
    void RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog);

    // IContextMenuProvider
    bool TargetExists(workspace::ContextMenuKind kind, const std::string& targetId) const override;
    bool IsItemEnabled(workspace::ContextMenuKind kind, std::string_view itemId, const std::string& targetId) const override;
    void OnItemChosen(workspace::ContextMenuKind kind, std::string_view itemId, const std::string& targetId) override;

private:
    // select_themeイベント。themeSetに無い名前は警告を出して無視する。
    void SelectTheme(const std::string& themeName);
    // select_gallery_segmentイベント(状態見本の先頭のセグメントの選択中の添字)。
    void SelectGallerySegment(int index);

    // rebuild_drag_cardsイベント(ドラッグ確認用のカード列を並べ直して要素を作り直させる)。
    void RebuildDragCards();
    // drop_cardイベント(落とされたカードの番号をログへ出す)。
    void DropCard(Rml::Event& event);

    // remove_context_cardイベント(右クリック確認用のカードの最後の1枚を消す)。
    void RemoveContextCard();
    // toggle_context_item_bイベント(項目Bの有効・無効を切り替える)。
    void ToggleContextItemB();

    // open_confirm_demoイベント(確認ダイアログを開き、確定とキャンセルの結果をログへ出す)。
    void OpenConfirmDemo();

    const UiThemeSet& themeSet_;
    RmlUiConfirmDialog* confirmDialog_ = nullptr;
    Rml::Context* context_ = nullptr;
    Rml::DataModelHandle modelHandle_;

    std::string theme_;      // 選択中のテーマ名(data-class-selectedが参照する)
    int gallerySegment_ = 1; // 状態見本の先頭のセグメントの選択中の添字(初期は「中」)
    std::vector<int> dragCards_; // ドラッグ確認用のカード番号
    std::string dropLog_;        // ドラッグ確認用の直近のドロップ結果
    std::vector<std::string> contextCards_; // 右クリック確認用のカードの対象id
    bool contextItemBEnabled_ = true;       // 項目Bを有効にするか
    std::string contextLog_;                // 右クリック確認用の直近の選択結果
    std::string confirmLog_;                // 確認ダイアログ確認用の直近の結果
    int confirmCount_ = 0;                  // 確認ダイアログを開いた回数(本文を変えて見分けるため)
};

} // namespace rs::app::rmlui

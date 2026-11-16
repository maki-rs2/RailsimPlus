//
// RmlUiContextMenu.h - 右クリックで出す汎用のコンテキストメニュー(文書に1つだけ置く要素 #context-menu)。
//
// 起動は属性で結線する。メニューを出したい要素に `data-context-menu="<種類のキー>"` と
// `data-context-id="<対象id>"` を付けると、右ボタンの mouseup を文書で受けて、ターゲットから親へたどって
// 最初に属性を持つ要素で開く。種類ごとの項目は ContextMenuCatalog.h の表、項目の有効・無効と選択後の処理は
// 種類ごとに登録する IContextMenuProvider が受け持つ。対象は要素でなくidで持つ
// (データバインディングで要素が作り直されるため)。
//
// 項目の要素はここで作り、メニューの外側から取る Element* は持たない(持つのは文書内に固定の #context-menu と、
// 開くたびに作り直す項目の要素だけ)。位置は大きさが決まるレイアウト後に Sync() で置く。
//
#pragma once

#include <array>
#include <string>
#include <vector>

#include <RmlUi/Core/EventListener.h>

#include "../workspace/ContextMenuCatalog.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
} // namespace Rml

namespace rs::app::rmlui {

// メニューが受け取るキー(上下で強調を移し、Enterで決定、Escで閉じる)。
enum class ContextMenuKey { Up, Down, Enter, Escape };

class RmlUiContextMenu : public Rml::EventListener {
public:
    // documentに #context-menu が無ければ何もしないメニューになる(開かない)。
    explicit RmlUiContextMenu(Rml::ElementDocument& document);
    ~RmlUiContextMenu() override;
    RmlUiContextMenu(const RmlUiContextMenu&) = delete;
    RmlUiContextMenu& operator=(const RmlUiContextMenu&) = delete;

    // 種類ごとの提供者。providerはこのメニューより長く生きること。
    void RegisterProvider(workspace::ContextMenuKind kind, workspace::IContextMenuProvider* provider);

    bool IsOpen() const { return open_; }
    void Close();

    // 開いていて、(x, y)がメニューの外なら閉じてtrueを返す(その押下は呼び出し側が消費する)。
    bool CloseIfOutside(float x, float y);

    // 開いている間のキー。扱ったらtrue(呼び出し側は他へ流さない)。開いていなければfalse。
    bool HandleKeyDown(ContextMenuKey key);

    // Rml::Context::Update() の後に毎フレーム呼ぶ。対象が無くなっていたら閉じ、開いた直後なら位置を置く。
    void Sync(const Rml::Context& context);

    void ProcessEvent(Rml::Event& event) override;

private:
    void Open(workspace::ContextMenuKind kind, const std::string& targetId, workspace::MenuPoint cursor);
    void OnMouseUp(Rml::Event& event);
    void OnMouseOver(Rml::Event& event);
    void OnClick(Rml::Event& event);
    // 項目を選ぶ。無効な項目は何もしない(開いたまま)。
    void ChooseItem(int index);
    // ターゲットから親へたどり、何番目の項目の中か(外なら-1)。
    int ItemIndexOf(Rml::Element* element) const;
    void SetHighlight(int index);
    void RefreshEnabled();
    std::vector<bool> EnabledFlags() const;

    Rml::ElementDocument& document_;
    Rml::Element* menu_ = nullptr; // 文書内に固定。nullなら無効
    std::array<workspace::IContextMenuProvider*, workspace::kContextMenuKindCount> providers_{};

    bool open_ = false;
    bool placed_ = false; // 位置を置いたか(開いた直後は大きさが未確定)
    workspace::ContextMenuKind kind_ = workspace::ContextMenuKind::ProtoDemo;
    std::string targetId_;
    workspace::MenuPoint cursor_;
    int highlight_ = -1;
    std::vector<Rml::Element*> itemElements_; // 開くたびに作り直す
};

} // namespace rs::app::rmlui

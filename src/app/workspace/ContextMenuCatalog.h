//
// ContextMenuCatalog.h - コンテキストメニュー(右クリックのメニュー)の種類ごとの項目の表と、
// 項目を受け持つ提供者のインターフェース、画面への置き方、強調の移し方。
//
// RmlUiには依存しない純C++。種類ごとの違いは表だけで表し、メニューの本体(RmlUiContextMenu)に
// 種類での分岐を書かない。場所を足すときは、RMLの属性・この表の行・提供者の3つだけを足す。
//
#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "CatalogCheck.h"
#include "EnumIndex.h"

namespace rs::app::workspace {

// メニューの種類。RMLの `data-context-menu` には列挙名の小文字を書く(RmlUiEnumKey.hのToEnumKey)。
enum class ContextMenuKind { ProtoDemo, FormationCard, LayingSet };
inline constexpr std::size_t kContextMenuKindCount = 3;

// 項目1行分。idは提供者へ渡す識別子、labelは表示文字列。
struct ContextMenuItemDef {
    const char* id;
    const char* label;
    bool danger;           // 赤字で出す(削除など)
    bool separatorBefore;  // 上に区切りの余白を入れる
};

inline constexpr ContextMenuItemDef kProtoDemoItems[] = {
    {"itemA", "項目A", false, false},
    {"itemB", "項目B(提供者が無効にする)", false, false},
    {"danger", "危険な項目", true, true},
};

inline constexpr ContextMenuItemDef kAddToTemplateItem{"addToTemplate", "テンプレートに追加", false, false};
inline constexpr ContextMenuItemDef kLayingSetItems[] = {kAddToTemplateItem};

inline constexpr ContextMenuItemDef kFormationCardItems[] = {
    kAddToTemplateItem,
    {"duplicate", "編成を複製", false, false},
    {"remove", "この編成を削除", true, false},
};

struct ContextMenuKindDef {
    ContextMenuKind kind;
    std::span<const ContextMenuItemDef> items;
};

// 列挙の順に1行ずつ並べる(下のstatic_assertで確かめる)。
inline constexpr ContextMenuKindDef kContextMenuCatalog[] = {
    {ContextMenuKind::ProtoDemo, kProtoDemoItems},
    {ContextMenuKind::FormationCard, kFormationCardItems},
    {ContextMenuKind::LayingSet, kLayingSetItems},
};

static_assert(std::size(kContextMenuCatalog) == kContextMenuKindCount, "kContextMenuCatalogの行数がContextMenuKindと一致しない");
static_assert(IsCatalogInEnumOrder(kContextMenuCatalog, &ContextMenuKindDef::kind),
              "kContextMenuCatalogの並びがContextMenuKindの列挙順と一致しない");

inline std::span<const ContextMenuItemDef> GetContextMenuItems(ContextMenuKind kind) {
    return kContextMenuCatalog[EnumIndex(kind)].items;
}

// 種類ごとに1つ登録する、メニューの持ち主。対象は要素へのポインタでなく文字列のidで持つ
// (データバインディングで要素が作り直されるため)。
class IContextMenuProvider {
public:
    virtual ~IContextMenuProvider() = default;
    // 対象がまだあるか。無くなったら開いているメニューを閉じる。
    virtual bool TargetExists(ContextMenuKind kind, const std::string& targetId) const = 0;
    // 項目を押せるか。押せない項目は暗く出し、選べない。
    virtual bool IsItemEnabled(ContextMenuKind kind, std::string_view itemId, const std::string& targetId) const = 0;
    // 項目が選ばれた。
    virtual void OnItemChosen(ContextMenuKind kind, std::string_view itemId, const std::string& targetId) = 0;
};

struct MenuPoint {
    float x = 0.0f;
    float y = 0.0f;
};

namespace detail {
// 1軸の置き方。カーソルの位置から開き、はみ出すなら反対側へ、それでも入らなければ画面の端に寄せる。
inline float PlaceMenuAxis(float cursor, float size, float viewport) {
    if (cursor + size <= viewport) return cursor;
    if (cursor - size >= 0.0f) return cursor - size;
    return std::max(0.0f, std::min(cursor, viewport - size));
}
} // namespace detail

// メニューの左上の置き場所。cursor・menuSize・viewportSizeは同じ単位(画面のpx)。
inline MenuPoint PlaceContextMenu(MenuPoint cursor, MenuPoint menuSize, MenuPoint viewportSize) {
    return {detail::PlaceMenuAxis(cursor.x, menuSize.x, viewportSize.x),
            detail::PlaceMenuAxis(cursor.y, menuSize.y, viewportSize.y)};
}

// 上下キーで強調を移す。無効な項目は飛ばし、端で反対側へ回る。current=-1は強調なし。
// direction=+1は下、-1は上。押せる項目が1つも無ければ-1。
inline int MoveContextMenuHighlight(const std::vector<bool>& enabled, int current, int direction) {
    const int count = static_cast<int>(enabled.size());
    if (count == 0) return -1;
    int index = current >= 0 ? current : (direction > 0 ? -1 : count);
    for (int step = 0; step < count; ++step) {
        index = ((index + direction) % count + count) % count;
        if (enabled[static_cast<std::size_t>(index)]) return index;
    }
    return -1;
}

} // namespace rs::app::workspace

//
// RmlUiInlineRename.h - 3D画面に追従する要素の名前を、その場の入力欄で変える共通の部品
//
// 閉塞のラベルと、番線・分岐のカードが使う。名前の表示と入力欄は、要素側の `is-editing` クラス
// (`editingId` が自分のidと同じとき)で切り替える。この部品は、編集中の対象・初期値・次のフレームへの
// フォーカス移しと、Enter・フォーカス喪失での確定、Escでの取り消しを受け持つ。確定した名前をどう
// 書き込むかは呼び出し側が決める。
//
#pragma once

#include <optional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../world/alignment/Guid.h"

namespace Rml {
class ElementDocument;
class Event;
}

namespace rs::app::rmlui {

class RmlUiInlineRename {
public:
    struct Commit {
        rs::world::Guid target = rs::world::NilGuid();
        std::string name;
    };

    // editingIdKeyはデータモデルへ結ぶ変数名、inputSelectorは編集中の入力欄を探すセレクタ。
    RmlUiInlineRename(const char* editingIdKey, const char* inputSelector);

    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 編集を始める。次のRefreshで入力欄へフォーカスを移して全選択する。idは要素のid属性と同じ値。
    void Begin(const std::string& id, const rs::world::Guid& target, std::string initialName,
               Rml::ElementDocument* document);
    void End();
    bool IsEditing(const std::string& id) const { return editingId_ == id; }
    bool Active() const { return !rs::world::GuidIsNil(target_); }
    const rs::world::Guid& Target() const { return target_; }

    // 毎フレーム呼ぶ。フォーカスの移しが残っていれば行う。
    void FocusPending(Rml::ElementDocument* document);
    // Enter(changeのlinebreak)とblurで編集を終え、確定する名前を返す。編集中でないとき・対象外のイベントはnullopt。
    // 確定先は編集を始めた対象で、blurのあとに選択が移っても変わらない。
    std::optional<Commit> OnCommit(Rml::Event& event);
    // Escで編集を取り消す。
    void OnKey(Rml::Event& event);

private:
    const char* editingIdKey_;
    const char* inputSelector_;
    Rml::String editingId_;
    rs::world::Guid target_ = rs::world::NilGuid();
    std::string initialName_;
    bool pendingFocus_ = false;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
};

} // namespace rs::app::rmlui

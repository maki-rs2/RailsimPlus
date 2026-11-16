//
// RmlUiFormationCarsView.h - 編成カードの「車両の絵」の操作(ゴミ箱・反転・`+`へのドロップ)をデータモデルへ結ぶ
//
// 車両の削除・向きの反転と、`+`(挿入位置)へのドラッグ&ドロップによる車両の追加・移動を受け持つ。
// ドロップできる荷物は3種類で、ブラウザのTrainカード(プラグインを挿入)、編成の絵の中の車両(移動)、
// 編成テンプレートのカード(テンプレートの車両をすべて挿入)。
// 荷物はドラッグを始めた時点で覚える(ドラッグ元の要素はデータバインディングで使い回されるため、
// 落とす時点では別のカードを指していることがある)。対象の編成はカードのguidで持ち、選択状態によらない。
// RmlUiFormationListViewがメンバとして持ち、同じデータモデル"workspace"へ変数とイベントを足す。
//
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../world/alignment/GuidJson.h"

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace rs::app {
struct FormationRefs;
class ToastQueue;
} // namespace rs::app

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

struct FormationDropState;

class RmlUiFormationCarsView {
public:
    RmlUiFormationCarsView();
    ~RmlUiFormationCarsView();
    RmlUiFormationCarsView(const RmlUiFormationCarsView&) = delete;
    RmlUiFormationCarsView& operator=(const RmlUiFormationCarsView&) = delete;

    // 編成の操作の対象と、ブラウザのカードの添字からフォルダを引くための持ち物、通知の出し先を渡す(どれもこのビューより長く生きること)。
    void Attach(const FormationRefs& refs, const plugin_browser::PluginBrowserService& browser, ToastQueue& queue);

    // データモデルの構築中に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    bool HandleKeyDown(unsigned int virtualKey, Rml::Element* hover);
    bool InsertPluginAfterFocused(int entryId);
    int FocusedCarIndex() const { return focusedCarIndex_; }

private:
    // ドラッグしている荷物の種類。
    enum class PayloadKind { None, Plugin, Car, Template };
    struct Payload {
        PayloadKind kind = PayloadKind::None;
        std::string folderUtf8;       // Plugin: 挿入するプラグインのフォルダ
        rs::world::Guid formationId;  // Car: 元の編成
        std::size_t carIndex = 0;     // Car: 元の車両の添字
        std::string templateName;     // Template: 挿入するテンプレートの名前
    };
    // 荷物とスロットから決まるドロップの内容。荷物の種類による分岐はResolveDropだけが持つ。
    struct DropPlan {
        std::size_t defIndex = 0;
        std::size_t slot = 0;
        Payload payload;
    };

    void BeginPluginDrag(int entryId);
    void BeginCarDrag(const std::string& cardId, int carIndex);
    void BeginTemplateDrag(const std::string& name);
    void EndDrag();
    void OnDragOver(Rml::Event& event, const std::string& cardId, int slot);
    void OnDragOut(Rml::Event& event, const std::string& cardId, int slot);
    void OnDrop(Rml::Event& event, const std::string& cardId, int slot);
    // カードのid(編成のguid)に対応する編成の添字。無ければ値なし。
    std::optional<std::size_t> FindDefIndex(const std::string& cardId) const;
    std::optional<DropPlan> ResolveDrop(const std::string& cardId, int slot) const;
    void SetDropTarget(const std::string& cardId, int slot);

    std::unique_ptr<FormationRefs> refs_;
    const plugin_browser::PluginBrowserService* browser_ = nullptr;
    ToastQueue* queue_ = nullptr;
    Payload payload_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<FormationDropState> dropState_;
    int focusedCarIndex_ = -1;
};

} // namespace rs::app::rmlui

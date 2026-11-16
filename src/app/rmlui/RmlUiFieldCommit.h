//
// RmlUiFieldCommit.h - 文字入力欄の確定のしかたをそろえる(Enter とフォーカスが外れたときに確定、Esc で取り消し)
//
// data-edit-commit 属性を持つ入力欄だけを対象にする。確定の処理そのものは各欄の blur のイベントが行い、
// ここは Enter でフォーカスを外し、Esc で入力前の値に戻してからフォーカスを外す。Esc で外したときは
// 欄に取り消しの印を付けるので、blur の処理は ConsumeFieldCancel で印を見て、確定せずに戻る。
// 文書より先に破棄すること(コンストラクタで足した聞き手をデストラクタで外す)。
//
#pragma once

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

class RmlUiFieldCommit : public Rml::EventListener {
public:
    explicit RmlUiFieldCommit(Rml::ElementDocument& document);
    ~RmlUiFieldCommit() override;
    RmlUiFieldCommit(const RmlUiFieldCommit&) = delete;
    RmlUiFieldCommit& operator=(const RmlUiFieldCommit&) = delete;

    void ProcessEvent(Rml::Event& event) override;

private:
    Rml::ElementDocument& document_;
};

// 欄が Esc で取り消されたか。印は1回読んだら消す(次の blur は通常どおり確定する)。
bool ConsumeFieldCancel(Rml::Element* element);

} // namespace rs::app::rmlui

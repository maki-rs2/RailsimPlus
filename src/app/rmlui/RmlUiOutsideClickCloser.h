//
// RmlUiOutsideClickCloser.h - 指定したクラスの要素の外側を押したとき、コールバックを呼ぶ(ポップオーバーを閉じる用)
//
// 文書に対してclickを捕捉段階(最初)で受け、押された要素とその親をたどって、insideClassesのどれかを
// 持つ要素の中なら何もしない。外側ならonOutsideを呼ぶ(押しはそのまま下へ届く)。
// 文書より先に破棄すること(コンストラクタで足した聞き手をデストラクタで外す)。
//
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

class RmlUiOutsideClickCloser : public Rml::EventListener {
public:
    RmlUiOutsideClickCloser(Rml::ElementDocument& document, std::vector<std::string> insideClasses,
                            std::function<void()> onOutside);
    ~RmlUiOutsideClickCloser() override;
    RmlUiOutsideClickCloser(const RmlUiOutsideClickCloser&) = delete;
    RmlUiOutsideClickCloser& operator=(const RmlUiOutsideClickCloser&) = delete;

    void ProcessEvent(Rml::Event& event) override;
    // UIに渡さないビューポートの押下でも、同じ外側判定を使う。targetはnullptrでもよい。
    void CloseIfOutside(Rml::Element* target);

private:
    Rml::ElementDocument& document_;
    std::vector<std::string> insideClasses_;
    std::function<void()> onOutside_;
};

} // namespace rs::app::rmlui

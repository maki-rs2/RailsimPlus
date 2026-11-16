//
// RmlUiClickSound.h - UIを押したときに効果音の要求を出す(クリック音・決定音・トグル音・タブ音)
//
// 文書のclickを受け、押された要素かその親にある最初の対象(button・checkbox/radioのinput・タブのsegment)で
// 要求を出す(無効なものは出さない)。確認ダイアログの確定ボタンは決定音、タブを切り替える要素はタブ音、
// checkbox/radioはトグル音、それ以外のボタンは通常のクリック音にする。タブの判定は、cppの表(クラス名)だけで決める。
// 文書より先に破棄すること(コンストラクタで足した聞き手をデストラクタで外す)。
//
#pragma once

#include <functional>

#include <RmlUi/Core/EventListener.h>

#include "../audio/UiSoundPlayer.h"

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

class RmlUiClickSound : public Rml::EventListener {
public:
    // onSoundには、鳴らす音の種類を渡す。
    RmlUiClickSound(Rml::ElementDocument& document, std::function<void(UiSound)> onSound);
    ~RmlUiClickSound() override;
    RmlUiClickSound(const RmlUiClickSound&) = delete;
    RmlUiClickSound& operator=(const RmlUiClickSound&) = delete;

    void ProcessEvent(Rml::Event& event) override;

private:
    Rml::ElementDocument& document_;
    std::function<void(UiSound)> onSound_;
};

} // namespace rs::app::rmlui

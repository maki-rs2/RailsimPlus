//
// RmlUiUpdatePhase.h - RmlUiのContext::Updateの最中かどうか
//
// データ結線による値の書き込み(data-value)や、data-forでの要素の作り直しはContext::Updateの中で起きる。
// ユーザーのクリック・キー操作はWin32メッセージの処理(RmlUiIntegration::HandleWin32Message)で届くので、
// Updateの外で起きる。RmlUiのselectはどちらの経路でも同じchangeイベントを出すため、ユーザーが変えたときだけ
// 値を書き込みたいイベント処理は、IsInsideContextUpdate()がtrueのchangeを無視する。
//
#pragma once

namespace rs::app::rmlui {

// Context::Updateを呼んでいる最中か。UIのスレッドからだけ呼ぶ。
bool IsInsideContextUpdate();

// 生きている間、IsInsideContextUpdate()をtrueにする。Context::Updateの呼び出しを囲む。
class ContextUpdateScope {
public:
    ContextUpdateScope();
    ~ContextUpdateScope();
    ContextUpdateScope(const ContextUpdateScope&) = delete;
    ContextUpdateScope& operator=(const ContextUpdateScope&) = delete;
};

} // namespace rs::app::rmlui

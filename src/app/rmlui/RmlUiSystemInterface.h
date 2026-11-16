//
// RmlUiSystemInterface.h - Rml::SystemInterface実装(タイマー・クリップボード・
// IME位置合わせ・ログ出力)とWin32 IME(WM_IME_*)処理。
//
// IME部分は RmlUi 6.3
// Backends/RmlUi_Platform_Win32.cpp の WindowProcedure 内のIME処理ロジックと
// TextInputMethodEditor_Win32 クラスを移植したもの(ウィンドウ生成・デバイス
// 初期化は含まない)。TextInputMethodEditor_Win32 はプラットフォーム汎用の
// ロジックであり上流の設計をそのまま使うため、クラス名は上流のまま残す
// (プロジェクトの命名規則は本クラスには適用しない判断)。
//
#pragma once

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/TextInputHandler.h>

#include <windows.h>

#include <chrono>

namespace Rml {
class Context;
}

namespace rs::app::rmlui {

// RmlUi_Platform_Win32.cpp の TextInputMethodEditor_Win32 を移植(上流の命名を保持)。
// IME変換中の文字列をRmlUiのテキスト入力欄(Rml::TextInputContext)へ反映する。
class TextInputMethodEditorWin32 final : public Rml::TextInputHandler {
public:
    TextInputMethodEditorWin32();

    void OnActivate(Rml::TextInputContext* inputContext) override;
    void OnDeactivate(Rml::TextInputContext* inputContext) override;
    void OnDestroy(Rml::TextInputContext* inputContext) override;

    bool IsComposing() const;
    void StartComposition();
    void CancelComposition();
    void SetComposition(Rml::StringView composition);
    void ConfirmComposition(Rml::StringView composition);
    void SetCursorPosition(int cursorPos, bool update);

private:
    void EndComposition();
    void DetachInputContext(Rml::TextInputContext* inputContext);
    void SetCompositionString(Rml::StringView composition);
    void UpdateCursorPosition();

    Rml::TextInputContext* inputContext_;
    bool composing_;
    int cursorPos_;
    int compositionRangeStart_;
    int compositionRangeEnd_;
};

// Rml::SystemInterface実装。タイマー・クリップボード・IME候補ウィンドウの位置合わせ・
// ログ出力(RCSS構文エラー等をspdlogへ転送する。
// RmlUi::Debuggerをリンクせずに拾うための経路)を担う。
class RmlUiSystemInterface : public Rml::SystemInterface {
public:
    explicit RmlUiSystemInterface(HWND windowHandle);

    double GetElapsedTime() override;
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;
    void ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    HWND windowHandle_;
    std::chrono::steady_clock::time_point startTime_;
};

// WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION/WM_IME_COMPOSITION/WM_IME_CHAR/
// WM_IME_REQUEST かどうかを判定する(MainWindowのWndProcが、これ以外のメッセージを
// HandleImeMessageへ渡さないようにするためのガード)。
bool IsImeMessage(UINT message);

// RmlUi_Platform_Win32.cpp の RmlWin32::WindowProcedure のうちIME関連の
// case文のみを移植したもの。戻り値はDefWindowProcへそのまま渡してよいかどうか
// (false=このメッセージは処理済みなのでDefWindowProcを呼ばない)。
bool HandleImeMessage(Rml::Context* context, TextInputMethodEditorWin32& editor, HWND windowHandle,
                      UINT message, WPARAM wParam, LPARAM lParam);

} // namespace rs::app::rmlui

#include "RmlUiSystemInterface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/TextInputContext.h>

#include <spdlog/spdlog.h>

#include <cstring>

#include "../../platform/text/Encoding.h"

// IMM(Input Method Manager)API。RmlUi_Platform_Win32.cppと同じ理由でここでリンクする。
#ifdef _MSC_VER
#pragma comment(lib, "imm32")
#endif

namespace rs::app::rmlui {

namespace {

int IMEGetCursorPosition(HIMC context) {
    return ImmGetCompositionString(context, GCS_CURSORPOS, nullptr, 0);
}

std::wstring IMEGetCompositionString(HIMC context, bool finalize) {
    const DWORD type = finalize ? GCS_RESULTSTR : GCS_COMPSTR;
    const int lenBytes = ImmGetCompositionString(context, type, nullptr, 0);
    if (lenBytes <= 0) return {};
    const int lenChars = lenBytes / static_cast<int>(sizeof(TCHAR));
    std::wstring buffer;
    buffer.resize(static_cast<size_t>(lenChars));
    ImmGetCompositionString(context, type, buffer.data(), lenBytes);
    return buffer;
}

} // namespace

// --- TextInputMethodEditor_Win32 (RmlUi_Platform_Win32.cppより移植) ---

TextInputMethodEditorWin32::TextInputMethodEditorWin32()
    : inputContext_(nullptr), composing_(false), cursorPos_(-1), compositionRangeStart_(0), compositionRangeEnd_(0) {}

void TextInputMethodEditorWin32::OnActivate(Rml::TextInputContext* inputContext) { inputContext_ = inputContext; }

void TextInputMethodEditorWin32::OnDeactivate(Rml::TextInputContext* inputContext) { DetachInputContext(inputContext); }

void TextInputMethodEditorWin32::OnDestroy(Rml::TextInputContext* inputContext) { DetachInputContext(inputContext); }

// 本体統合: 現在の入力先が外れるときは変換状態も初期値へ戻す(上流は入力先を外すだけで、
// 変換中にフォーカスが移ると次の入力欄で古い範囲を使ってしまう)。外れる入力先へは何も書き込まない。
void TextInputMethodEditorWin32::DetachInputContext(Rml::TextInputContext* inputContext) {
    if (inputContext_ != inputContext) return;
    inputContext_ = nullptr;
    composing_ = false;
    cursorPos_ = -1;
    compositionRangeStart_ = 0;
    compositionRangeEnd_ = 0;
}

bool TextInputMethodEditorWin32::IsComposing() const { return composing_; }

void TextInputMethodEditorWin32::StartComposition() { composing_ = true; }

void TextInputMethodEditorWin32::EndComposition() {
    if (inputContext_ != nullptr) inputContext_->SetCompositionRange(0, 0);
    composing_ = false;
    compositionRangeStart_ = 0;
    compositionRangeEnd_ = 0;
}

void TextInputMethodEditorWin32::CancelComposition() {
    if (inputContext_ != nullptr) {
        inputContext_->SetText(Rml::StringView(), compositionRangeStart_, compositionRangeEnd_);
        inputContext_->SetCursorPosition(compositionRangeStart_);
    }
    EndComposition();
}

void TextInputMethodEditorWin32::SetComposition(Rml::StringView composition) {
    SetCompositionString(composition);
    UpdateCursorPosition();
    if (cursorPos_ != -1 && inputContext_ != nullptr)
        inputContext_->SetCompositionRange(compositionRangeStart_, compositionRangeEnd_);
}

void TextInputMethodEditorWin32::ConfirmComposition(Rml::StringView composition) {
    SetCompositionString(composition);
    if (inputContext_ != nullptr) {
        inputContext_->SetCompositionRange(compositionRangeStart_, compositionRangeEnd_);
        inputContext_->CommitComposition(composition);
    }
    SetCursorPosition(compositionRangeEnd_ - compositionRangeStart_, true);
    EndComposition();
}

void TextInputMethodEditorWin32::SetCursorPosition(int cursorPos, bool update) {
    cursorPos_ = cursorPos;
    if (update) UpdateCursorPosition();
}

void TextInputMethodEditorWin32::SetCompositionString(Rml::StringView composition) {
    if (inputContext_ == nullptr) return;
    if (compositionRangeStart_ == 0 && compositionRangeEnd_ == 0)
        inputContext_->GetSelectionRange(compositionRangeStart_, compositionRangeEnd_);
    inputContext_->SetText(composition, compositionRangeStart_, compositionRangeEnd_);
    const size_t length = Rml::StringUtilities::LengthUTF8(composition);
    compositionRangeEnd_ = compositionRangeStart_ + static_cast<int>(length);
}

void TextInputMethodEditorWin32::UpdateCursorPosition() {
    if (inputContext_ == nullptr || (compositionRangeStart_ == 0 && compositionRangeEnd_ == 0)) return;
    if (cursorPos_ != -1) {
        inputContext_->SetCursorPosition(compositionRangeStart_ + cursorPos_);
    } else {
        inputContext_->SetSelectionRange(compositionRangeStart_, compositionRangeEnd_);
    }
}

// --- RmlUiSystemInterface ---

RmlUiSystemInterface::RmlUiSystemInterface(HWND windowHandle)
    : windowHandle_(windowHandle), startTime_(std::chrono::steady_clock::now()) {}

double RmlUiSystemInterface::GetElapsedTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

void RmlUiSystemInterface::SetClipboardText(const Rml::String& text) {
    if (windowHandle_ == nullptr) return;
    // 本体統合: 先にメモリの確保と書き込みを済ませ、成功したときだけクリップボードを
    // 空にして渡す(失敗したら元の内容を消さない)。
    const std::wstring wide = rs::platform_text::Utf8ToWide(std::string(text.c_str(), text.size()));
    const size_t byteSize = sizeof(wchar_t) * (wide.size() + 1);
    HGLOBAL clipboardData = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (clipboardData == nullptr) return;
    void* locked = GlobalLock(clipboardData);
    if (locked == nullptr) {
        GlobalFree(clipboardData);
        return;
    }
    std::memcpy(locked, wide.c_str(), byteSize);
    GlobalUnlock(clipboardData);

    if (!OpenClipboard(windowHandle_)) {
        GlobalFree(clipboardData);
        return;
    }
    EmptyClipboard();
    // 成功するとメモリの持ち主がシステムへ移る。失敗したときだけ自分で解放する。
    if (SetClipboardData(CF_UNICODETEXT, clipboardData) == nullptr) GlobalFree(clipboardData);
    CloseClipboard();
}

void RmlUiSystemInterface::GetClipboardText(Rml::String& text) {
    if (windowHandle_ == nullptr) return;
    if (!OpenClipboard(windowHandle_)) return;
    HANDLE clipboardData = GetClipboardData(CF_UNICODETEXT);
    if (clipboardData != nullptr) {
        const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(clipboardData));
        if (wide != nullptr) {
            const std::string utf8 = rs::platform_text::WideToUtf8(wide);
            text = Rml::String(utf8.c_str(), utf8.size());
            GlobalUnlock(clipboardData);
        }
    }
    CloseClipboard();
}

void RmlUiSystemInterface::ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) {
    if (windowHandle_ == nullptr) return;
    HIMC himc = ImmGetContext(windowHandle_);
    if (himc == nullptr) return;

    constexpr LONG kBottomMargin = 2;
    const LONG x = static_cast<LONG>(caretPosition.x);
    const LONG y = static_cast<LONG>(caretPosition.y);
    const LONG w = 1;
    const LONG h = static_cast<LONG>(lineHeight) + kBottomMargin;

    COMPOSITIONFORM comp = {};
    comp.dwStyle = CFS_FORCE_POSITION;
    comp.ptCurrentPos = {x, y};
    ImmSetCompositionWindow(himc, &comp);

    CANDIDATEFORM cand = {};
    cand.dwStyle = CFS_EXCLUDE;
    cand.ptCurrentPos = {x, y};
    cand.rcArea = {x, y, x + w, y + h};
    ImmSetCandidateWindow(himc, &cand);

    ImmReleaseContext(windowHandle_, himc);
}

bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    // RmlUi::Debuggerをリンクせずに、RCSS構文エラー等のログ(Event Log相当)をspdlogへ
    // 転送する経路。
    const std::string text(message.c_str(), message.size());
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            spdlog::error("[rmlui] {}", text);
            break;
        case Rml::Log::LT_WARNING:
            spdlog::warn("[rmlui] {}", text);
            break;
        default:
            spdlog::info("[rmlui] {}", text);
            break;
    }
    return true;
}

bool IsImeMessage(UINT message) {
    switch (message) {
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_ENDCOMPOSITION:
        case WM_IME_COMPOSITION:
        case WM_IME_CHAR:
        case WM_IME_REQUEST:
            return true;
        default:
            return false;
    }
}

bool HandleImeMessage(Rml::Context* context, TextInputMethodEditorWin32& editor, HWND windowHandle, UINT message,
                       WPARAM /*wParam*/, LPARAM lParam) {
    if (context == nullptr) return true;

    bool result = true;
    switch (message) {
        case WM_IME_STARTCOMPOSITION:
            editor.StartComposition();
            // 既定のIME(未確定)ウィンドウが出るのを防ぐため、このメッセージは消費する。
            result = false;
            break;
        case WM_IME_ENDCOMPOSITION:
            if (editor.IsComposing()) editor.ConfirmComposition(Rml::StringView());
            break;
        case WM_IME_COMPOSITION: {
            HIMC immContext = ImmGetContext(windowHandle);
            if (immContext == nullptr) break;

            if (!editor.IsComposing()) editor.StartComposition();

            if ((lParam & GCS_CURSORPOS) != 0) {
                int cursorPos = IMEGetCursorPosition(immContext);
                const std::wstring composition = IMEGetCompositionString(immContext, false);
                const std::string converted =
                    rs::platform_text::WideToUtf8(composition.substr(0, static_cast<size_t>(cursorPos)));
                cursorPos = static_cast<int>(Rml::StringUtilities::LengthUTF8(Rml::StringView(converted)));
                editor.SetCursorPosition(cursorPos, true);
            }

            if ((lParam & CS_NOMOVECARET) != 0) editor.SetCursorPosition(-1, false);

            if ((lParam & GCS_RESULTSTR) != 0) {
                const std::wstring composition = IMEGetCompositionString(immContext, true);
                const std::string converted = rs::platform_text::WideToUtf8(composition);
                editor.ConfirmComposition(Rml::StringView(converted));
            }

            if ((lParam & GCS_COMPSTR) != 0) {
                const std::wstring composition = IMEGetCompositionString(immContext, false);
                const std::string converted = rs::platform_text::WideToUtf8(composition);
                editor.SetComposition(Rml::StringView(converted));
            }

            if (lParam == 0) editor.CancelComposition();

            ImmReleaseContext(windowHandle, immContext);
            break;
        }
        case WM_IME_CHAR:
        case WM_IME_REQUEST:
            // WM_IME_CHARはIME確定文字列をWM_CHARとして重複配信する既定挙動を防ぐため消費する。
            result = false;
            break;
        default:
            break;
    }
    return result;
}

} // namespace rs::app::rmlui

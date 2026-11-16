#include "StartupFailureDialog.h"

#include <windows.h>

#include <format>

#include "../../platform/text/Encoding.h"
#include "../locale/Localization.h"

namespace rs::app {

void ShowFatalErrorBox(const std::string& title, const std::string& body) {
    // キューに WM_QUIT が残っていると、MessageBox は表示せずにすぐ戻る。終了する直前なので捨ててよい。
    MSG quit;
    while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
    MessageBoxW(nullptr, rs::platform_text::Utf8ToWide(body).c_str(),
                rs::platform_text::Utf8ToWide(title).c_str(),
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

void ShowStartupFailureDialog(const Localization& text, const std::string& reason) {
    const std::string body =
        std::vformat(text.Text("app.startup_failed.body"), std::make_format_args(reason));
    ShowFatalErrorBox(text.Text("app.startup_failed.title"), body);
}

} // namespace rs::app

// RmlUiIntegrationTheme.cpp - RmlUiIntegrationのUIテーマの適用と切替。
// 文書の読込時にテーマのスタイルシートを足して有効にする処理と、実行中のテーマ切替の入り口(SetUiTheme)。
#include "RmlUiIntegration.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <spdlog/spdlog.h>

namespace rs::app::rmlui {

void RmlUiIntegration::UiThemePlugin::OnDocumentLoad(Rml::ElementDocument* document) {
    if (document == nullptr) return;
    if (!ApplyUiThemeStyleSheet(*document, owner_.themeRcss_)) {
        spdlog::warn("[rmlui] UIテーマの適用に失敗しました(既定スタイルのまま続行します): {}",
                     document->GetSourceURL());
        return;
    }
    if (owner_.activeTheme_.empty()) return;
    Rml::Context* const context = document->GetContext();
    if (context != nullptr) ActivateUiTheme(*context, owner_.uiThemeSet_, owner_.activeTheme_);
}

void RmlUiIntegration::SetUiTheme(const std::string& themeName) {
    const std::string resolved = ResolveUiThemeName(uiThemeSet_, themeName);
    if (resolved == activeTheme_) return;
    activeTheme_ = resolved;
    if (context_ != nullptr && !activeTheme_.empty()) ActivateUiTheme(*context_, uiThemeSet_, activeTheme_);
}

} // namespace rs::app::rmlui

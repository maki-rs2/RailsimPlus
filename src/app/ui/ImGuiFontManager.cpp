#include "ImGuiFontManager.h"

#include <spdlog/spdlog.h>

#include "imgui_impl_dx12.h"

#include "../../platform/font/UiFont.h"
#include "../../plugins/load/PluginLoader.h"

namespace rs::app {

void BuildJapaneseFontAtlas(ImGuiIO& io, ImVector<ImWchar>& ranges,
                            const rs::legacy_plugin::LoadedPlugin* plugin,
                            bool rebuildBackend) {
    if (rebuildBackend) io.Fonts->Clear();

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    if (plugin) {
        builder.AddText(plugin->def.header.name.c_str());
        builder.AddText(plugin->def.header.author.c_str());
        for (const auto& sw : plugin->def.switches) {
            builder.AddText(sw.name.c_str());
            builder.AddText(sw.groupCommon.c_str());
            for (const auto& e : sw.entries) builder.AddText(e.c_str());
        }
        for (const auto& m : plugin->models) builder.AddText(m.name.c_str());
    }
    ranges.clear();
    builder.BuildRanges(&ranges);

    const std::string fontPath = rs::platform_font::ResolveUiFontPath();
    if (!fontPath.empty()) {
        // 可変フォントの太さ(SemiBold)を名前付きインスタンス経由で指定する。
        ImFontConfig fontConfig;
        fontConfig.FontNo = static_cast<int>(
            rs::platform_font::FindNamedInstanceFaceIndex(fontPath, rs::platform_font::kUiFontWeight));
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, &fontConfig, ranges.Data);
    } else {
        spdlog::warn(
            "Japanese font not found ({}); falling back to default font",
            rs::platform_font::kUiFontRelativePath);
        io.Fonts->AddFontDefault();
    }

    if (rebuildBackend) {
        io.Fonts->Build();
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }
}

} // namespace rs::app

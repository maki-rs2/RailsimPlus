#include "RenderTuningHotReload.h"

#include <spdlog/spdlog.h>

#include <utility>

#include "../../platform/file/AssetPath.h"
#include "../../platform/file/FileRead.h"

namespace rs::app {

namespace {

// 確認の間隔[秒]。
constexpr double kCheckIntervalSeconds = 0.5;
constexpr const char* kRenderTuningAssetPath = "assets/config/render_tuning.json";

std::optional<rs::render::RenderTuning> ParseOrWarn(const std::string& text) {
    std::optional<rs::render::RenderTuning> parsed = rs::render::RenderTuning::TryParseJson(text);
    if (!parsed) spdlog::warn("[tuning] render_tuning.json を読めなかった。前の値のまま続ける");
    return parsed;
}

}  // namespace

std::optional<rs::render::RenderTuning> PollRenderTuningReload(rs::platform_file::StableFileWatcher& watcher,
                                                               double nowSeconds) {
    const std::optional<std::string> text = watcher.Poll(nowSeconds);
    if (!text) return std::nullopt;
    return ParseOrWarn(*text);
}

std::optional<rs::render::RenderTuning> ReadRenderTuningFile(const std::filesystem::path& path) {
    const std::optional<std::string> text = rs::platform_file::ReadFileText(path);
    if (!text) return std::nullopt;
    return ParseOrWarn(*text);
}

void LoadRenderTuningFromSource() {
    std::optional<rs::render::RenderTuning> loaded =
        ReadRenderTuningFile(rs::platform_file::ResolveAssetPath(kRenderTuningAssetPath));
    if (!loaded) return;
    rs::render::RenderTuning::PublishReloaded(std::move(*loaded));
    spdlog::info("[tuning] 起動時に render_tuning.json を読んだ");
}

void TickRenderTuningHotReload(std::optional<rs::platform_file::StableFileWatcher>& watcher, double nowSeconds) {
    if (!watcher) {
        // ソースツリー上のJSONを見る(見つからなければ空パスになり、ファイルが無い扱いで何も起きない)。
        watcher.emplace(rs::platform_file::ResolveAssetPath(kRenderTuningAssetPath), kCheckIntervalSeconds);
    }
    std::optional<rs::render::RenderTuning> reloaded = PollRenderTuningReload(*watcher, nowSeconds);
    if (!reloaded) return;
    rs::render::RenderTuning::PublishReloaded(std::move(*reloaded));
    spdlog::info("[tuning] render_tuning.json を読み直した");
}

}  // namespace rs::app

#include "TemplateImageStore.h"

#include <charconv>
#include <filesystem>
#include <functional>
#include <string_view>
#include <system_error>

#include "../../platform/text/Encoding.h"
#include "../plugin_session/RailwayPluginSetResolve.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "LayingSetPreviewStore.h"

namespace rs::app::plugin_browser {
namespace {

constexpr std::string_view kImagePrefix = "tmimg:";

void Mix(std::uint64_t& seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

// テンプレートが参照するプラグインのフォルダ(更新時刻を調べる対象)。
std::vector<std::string> DependencyFolders(const TemplateImageSource& source) {
    std::vector<std::string> folders;
    if (source.kind == TemplateImageKind::Rail) {
        return ResolveRailwayPluginSetFile(source.pathUtf8, source.rootUtf8).folders;
    }
    const TrainGroupTemplateParseResult parsed = ParseTrainGroupTemplateFile(source.pathUtf8, source.rootUtf8);
    for (const FormationCarSpec& spec : parsed.cars) {
        std::string folder = ResolveTrainCarFolder(parsed.installRootUtf8, spec);
        if (!folder.empty()) folders.push_back(std::move(folder));
    }
    return folders;
}

} // namespace

TemplateImageKey MakeTemplateImageKey(const TemplateImageSource& source) {
    // プラグインの定義ファイルの更新時刻・読込結果の世代は、敷設セットの合成と同じ作り方を共用する。
    std::uint64_t key = MakeLayingPreviewKey(DependencyFolders(source));
    Mix(key, static_cast<std::uint64_t>(source.kind));
    Mix(key, std::hash<std::string>()(source.pathUtf8));
    std::error_code error;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(rs::platform_text::Utf8ToWide(source.pathUtf8), error);
    Mix(key, error ? 0u : static_cast<std::uint64_t>(writeTime.time_since_epoch().count()));
    return key;
}

std::string MakeTemplateImageName(TemplateImageKey key) {
    return std::string(kImagePrefix) + std::to_string(key);
}

std::optional<TemplateImageKey> ParseTemplateImageName(const std::string& name) {
    const std::string_view view(name);
    if (!view.starts_with(kImagePrefix)) return std::nullopt;
    const std::string_view digits = view.substr(kImagePrefix.size());
    TemplateImageKey key = 0;
    const std::from_chars_result result = std::from_chars(digits.data(), digits.data() + digits.size(), key);
    if (result.ec != std::errc() || result.ptr != digits.data() + digits.size()) return std::nullopt;
    return key;
}

void TemplateImageStore::Request(const std::vector<WantedTemplateImage>& wanted, IPluginImagePoster& poster) {
    for (const WantedTemplateImage& item : wanted) {
        if (pending_.size() >= kMaxPendingTemplateImages) return;
        if (pending_.contains(item.key) || failed_.contains(item.key) || Find(item.key) != nullptr) continue;

        PluginImageRequest request;
        request.kind = item.source.kind == TemplateImageKind::Train ? ImageKind::TrainTemplate
                                                                    : ImageKind::RailTemplate;
        request.templatePathUtf8 = item.source.pathUtf8;
        request.templateRootUtf8 = item.source.rootUtf8;
        request.templateKey = item.key;
        pending_.insert(item.key);
        poster.Post(std::move(request), kTemplateImagePriority);
    }
}

void TemplateImageStore::Accept(const PluginImageResult& result) {
    pending_.erase(result.templateKey);
    if (!result.ok || !result.image.ok) {
        failed_.insert(result.templateKey);
        return;
    }
    for (std::list<Cached>::iterator it = lru_.begin(); it != lru_.end(); ++it) {
        if (it->key == result.templateKey) {
            lru_.erase(it);
            break;
        }
    }
    lru_.push_front(Cached{result.templateKey, result.image});
    while (lru_.size() > maxCached_) lru_.pop_back();
}

const rs::render::ImageData* TemplateImageStore::Find(TemplateImageKey key) const {
    for (const Cached& cached : lru_) {
        if (cached.key == key) return &cached.image;
    }
    return nullptr;
}

const rs::render::ImageData* TemplateImageStore::FindByName(const std::string& name) const {
    const std::optional<TemplateImageKey> key = ParseTemplateImageName(name);
    return key.has_value() ? Find(*key) : nullptr;
}

} // namespace rs::app::plugin_browser

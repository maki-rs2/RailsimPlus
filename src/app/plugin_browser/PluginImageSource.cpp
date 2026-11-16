#include "PluginImageSource.h"

#include <charconv>
#include <string_view>

namespace rs::app::plugin_browser {
namespace {

constexpr std::string_view kPrefix = "pbimg:";
constexpr char kSeparator = '/';

} // namespace

std::string MakeImageSourceName(const PluginImageSource& source) {
    std::string name = std::string(kPrefix) + std::to_string(source.generation) + kSeparator + std::to_string(source.key.id);
    if (source.surface == ImageSurface::FormationCard) {
        name += kSeparator;
        name += GetThumbnailView(source.key.view).sourceTag;
    }
    return name;
}

std::optional<PluginImageSource> ParseImageSourceName(const std::string& name) {
    const std::string_view view(name);
    if (!view.starts_with(kPrefix)) return std::nullopt;
    const std::string_view rest = view.substr(kPrefix.size());
    const std::size_t separator = rest.find(kSeparator);
    if (separator == std::string_view::npos) return std::nullopt;

    PluginImageSource source;
    const std::string_view generationText = rest.substr(0, separator);
    std::string_view idText = rest.substr(separator + 1);
    const std::size_t tagSeparator = idText.find(kSeparator);
    if (tagSeparator != std::string_view::npos) {
        const std::string_view tagText = idText.substr(tagSeparator + 1);
        idText = idText.substr(0, tagSeparator);
        bool found = false;
        for (const ThumbnailViewTraits& traits : kThumbnailViews) {
            if (tagText.size() == 1 && tagText[0] == traits.sourceTag) {
                source.key.view = traits.view;
                found = true;
            }
        }
        if (!found) return std::nullopt;
        source.surface = ImageSurface::FormationCard;
    }
    unsigned long long id = 0;
    const std::from_chars_result generationResult =
        std::from_chars(generationText.data(), generationText.data() + generationText.size(), source.generation);
    const std::from_chars_result idResult = std::from_chars(idText.data(), idText.data() + idText.size(), id);
    if (generationResult.ec != std::errc() || generationResult.ptr != generationText.data() + generationText.size()) {
        return std::nullopt;
    }
    if (idResult.ec != std::errc() || idResult.ptr != idText.data() + idText.size()) return std::nullopt;
    source.key.id = static_cast<EntryId>(id);
    return source;
}

} // namespace rs::app::plugin_browser

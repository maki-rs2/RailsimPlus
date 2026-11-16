#include "RailSoundLookup.h"

#include <cmath>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginPaths.h"

namespace rs::app {

bool IsWheelSoundAudible(const rs::legacy_pi::RailSoundDef& sound) {
    return sound.hasSoundInfo && !sound.wheelSoundFile.empty() && std::isfinite(sound.jointInterval) &&
           sound.jointInterval > 0.0f;
}

const TrackPluginEntry* FindWheelSoundRail(const rs::world::TrackEdge& edge, double s,
                                           const TrackPluginFinder& findEntry) {
    if (edge.pluginSections.SpanCount() == 0) return nullptr;
    for (const std::string& folder : edge.pluginSections.ValueAt(s).plugins) {
        const TrackPluginEntry* entry = findEntry(folder);
        if (entry == nullptr || entry->type != rs::legacy_pi::PluginType::Rail) continue;
        return IsWheelSoundAudible(entry->def.railSound) ? entry : nullptr;
    }
    return nullptr;
}

std::wstring ResolveWheelSoundPath(const TrackPluginEntry& rail) {
    return rs::legacy_plugin::ResolvePath(rs::platform_text::Utf8ToWide(rail.folderUtf8),
                                          rail.def.railSound.wheelSoundFile);
}

} // namespace rs::app

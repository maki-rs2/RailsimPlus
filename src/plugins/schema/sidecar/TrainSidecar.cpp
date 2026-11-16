//
// TrainSidecar.cpp - 列車向けプラグイン拡張JSONの読込
//
#include "TrainSidecar.h"

#include <set>

#include "PluginSidecarParsing.h"
#include "../../../platform/text/Encoding.h"

namespace rs::legacy_pi::sidecar {

TrainSidecar LoadTrainSidecar(const std::wstring& pluginFolder) {
    TrainSidecar sidecar;
    rs::legacy_pi::Diagnostics diag;
    const std::string diagContext = rs::platform_text::WideToUtf8(pluginFolder);
    const std::optional<nlohmann::json> root = ReadSidecarJsonFile(
        pluginFolder, kTrainSidecarFileName, diagContext, diag);
    if (!root.has_value()) {
        sidecar.diagnostics = diag.Items();
        return sidecar;
    }

    const std::set<std::string> kReservedKeys{"sounds"};
    sidecar.table = ParseSwitchMappingTable(*root, kReservedKeys, diagContext, diag);
    if (root->contains("sounds")) {
        sidecar.hasSounds = true;
        sidecar.soundsRawJson = (*root)["sounds"].dump();
    }
    sidecar.loaded = true;
    sidecar.diagnostics = diag.Items();
    return sidecar;
}

} // namespace rs::legacy_pi::sidecar

#include "RailwayPluginSetSchema.h"

#include "PiValueAccessors.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiBlock;

} // namespace

RailwayPluginSetDef ParseRailwayPluginSet(const rs::legacy_pi::PiDocument& doc,
                                          std::vector<std::string>& warnings) {
    RailwayPluginSetDef out;

    const PiBlock* header = nullptr;
    const PiBlock* set = nullptr;
    for (const auto& n : doc.nodes) {
        if (n.name == "DatafileHeader") header = &n;
        else if (n.name == "RailwayPluginSet") set = &n;
    }
    if (!header) warnings.push_back("DatafileHeader が見つかりません");
    if (!set) {
        warnings.push_back("RailwayPluginSet ブロックが見つかりません");
        return out;
    }

    out.found = true;
    out.settingName = GetString(*set, "SettingName");

    out.railPlugin   = GetString(*set, "RailPlugin");
    out.railCheck    = GetYesNo(*set, "RailCheck", false);
    out.tiePlugin    = GetString(*set, "TiePlugin");
    out.tieCheck     = GetYesNo(*set, "TieCheck", false);
    out.girderPlugin = GetString(*set, "GirderPlugin");
    out.girderCheck  = GetYesNo(*set, "GirderCheck", false);
    out.pierPlugin   = GetString(*set, "PierPlugin");
    out.pierCheck    = GetYesNo(*set, "PierCheck", false);
    out.linePlugin   = GetString(*set, "LinePlugin");
    out.lineCheck    = GetYesNo(*set, "LineCheck", false);
    out.polePlugin   = GetString(*set, "PolePlugin");
    out.poleCheck    = GetYesNo(*set, "PoleCheck", false);

    out.enableCant      = GetYesNo(*set, "EnableCant", false);
    out.liftRailSurface = GetYesNo(*set, "LiftRailSurface", false);
    out.multiTrack       = GetYesNo(*set, "MultiTrack", false);
    out.trackNum         = GetInt(*set, "TrackNum", 1);
    out.trackInterval    = GetFloat(*set, "TrackInterval", 0.0f);

    for (const auto& n : doc.nodes) {
        if (n.name != "RailsimPlusExtra") continue;
        for (const rs::legacy_pi::PiAssignment* a : FindAllAssignments(n, "Plugin")) {
            if (!a->values.empty()) out.extraPlugins.push_back(a->values[0].AsString());
        }
    }

    return out;
}

} // namespace rs::legacy_layout

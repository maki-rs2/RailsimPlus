#include "RailwayPluginSetWriter.h"

#include <array>
#include <format>

namespace rs::legacy_layout {

namespace {

// 種類ごとの違いは表で持つ(Plugin名・Checkのキーとメンバ)。
struct KindColumn {
    const char* pluginKey;
    const char* checkKey;
    std::string RailwayPluginSetDef::* plugin;
    bool RailwayPluginSetDef::* check;
};

constexpr std::array<KindColumn, 6> kKinds{{
    {"RailPlugin", "RailCheck", &RailwayPluginSetDef::railPlugin, &RailwayPluginSetDef::railCheck},
    {"TiePlugin", "TieCheck", &RailwayPluginSetDef::tiePlugin, &RailwayPluginSetDef::tieCheck},
    {"GirderPlugin", "GirderCheck", &RailwayPluginSetDef::girderPlugin,
     &RailwayPluginSetDef::girderCheck},
    {"PierPlugin", "PierCheck", &RailwayPluginSetDef::pierPlugin, &RailwayPluginSetDef::pierCheck},
    {"LinePlugin", "LineCheck", &RailwayPluginSetDef::linePlugin, &RailwayPluginSetDef::lineCheck},
    {"PolePlugin", "PoleCheck", &RailwayPluginSetDef::polePlugin, &RailwayPluginSetDef::poleCheck},
}};

const char* YesNo(bool value) { return value ? "yes" : "no"; }

// 読み戻しで同じ値に戻る最短の表記。整数値にも小数点を付けて数値だと分かるようにする。
std::string FormatFloat(float value) {
    std::string text = std::format("{}", value);
    if (text.find_first_of(".en") == std::string::npos) text += ".0";
    return text;
}

}  // namespace

bool HasExtraPlugins(const RailwayPluginSetDef& def) { return !def.extraPlugins.empty(); }

TemplateWriteResult WriteRailwayPluginSet(const RailwayPluginSetDef& def) {
    TemplateWriteResult result;
    const auto fail = [&result](const char* field) {
        result.badField = field;
        return result;
    };

    if (!IsWritableTemplateString(def.settingName)) return fail("SettingName");
    for (const KindColumn& kind : kKinds)
        if (!IsWritableTemplateString(def.*kind.plugin)) return fail(kind.pluginKey);
    for (const std::string& extra : def.extraPlugins)
        if (!IsWritableTemplateString(extra)) return fail("RailsimPlusExtra.Plugin");

    // 改行はCRLF、字下げはタブ(FORMATS §2)。
    std::string& out = result.text;
    out += "DatafileHeader{\r\n"
           "\tRailSimVersion = 2.15;\r\n"
           "\tDatafileType = RailwayPluginSet;\r\n"
           "}\r\n"
           "RailwayPluginSet{\r\n";
    out += "\tSettingName = " + QuoteTemplateString(def.settingName) + ";\r\n";
    for (const KindColumn& kind : kKinds) {
        out += std::string("\t") + kind.pluginKey + " = " + QuoteTemplateString(def.*kind.plugin) +
               ";\r\n";
        out += std::string("\t") + kind.checkKey + " = " + YesNo(def.*kind.check) + ";\r\n";
    }
    out += std::string("\tEnableCant = ") + YesNo(def.enableCant) + ";\r\n";
    out += std::string("\tLiftRailSurface = ") + YesNo(def.liftRailSurface) + ";\r\n";
    out += std::string("\tMultiTrack = ") + YesNo(def.multiTrack) + ";\r\n";
    out += "\tTrackNum = " + std::to_string(def.trackNum) + ";\r\n";
    out += "\tTrackInterval = " + FormatFloat(def.trackInterval) + ";\r\n";
    out += "}\r\n";

    if (HasExtraPlugins(def)) {
        out += "RailsimPlusExtra{\r\n";
        for (const std::string& extra : def.extraPlugins)
            out += "\tPlugin = " + QuoteTemplateString(extra) + ";\r\n";
        out += "}\r\n";
    }

    result.ok = true;
    return result;
}

}  // namespace rs::legacy_layout

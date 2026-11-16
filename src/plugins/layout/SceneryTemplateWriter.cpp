#include "SceneryTemplateWriter.h"

#include <format>

namespace rs::legacy_layout {

namespace {

// floatの{}は最短の往復可能な形になる。
std::string VecLine(const char* key, const rs::core::Vec3& v) {
    return std::format("\t\t{} = ({}, {}, {});\r\n", key, v.x, v.y, v.z);
}

}  // namespace

TemplateWriteResult WriteSceneryTemplate(const SceneryTemplateDef& def) {
    const bool station = def.kind == SceneryTemplateKind::Station;
    const char* pluginKey = station ? "StationPlugin" : "StructPlugin";

    TemplateWriteResult result;
    for (const SceneryTemplateEntry& entry : def.entries) {
        if (!IsWritableTemplateString(entry.plugin)) {
            result.badField = pluginKey;
            return result;
        }
    }

    const std::string boxName = station ? "StationBuildTemplate" : "StructBuildTemplate";
    const char* childName = station ? "StationTemplate" : "StructTemplate";

    std::string& out = result.text;
    out += "DatafileHeader{\r\n"
           "\tRailSimVersion = 2.15;\r\n"
           "\tDatafileType = " + boxName + ";\r\n"
           "}\r\n" + boxName + "{\r\n";
    for (const SceneryTemplateEntry& entry : def.entries) {
        out += std::string("\t") + childName + "{\r\n";
        out += std::string("\t\t") + pluginKey + " = " + QuoteTemplateString(entry.plugin) + ";\r\n";
        if (!entry.switchOptions.empty()) {
            out += "\t\tSwitchNum = " + std::to_string(entry.switchOptions.size()) + ";\r\n";
            out += "\t\tSwitchOption = ";
            for (size_t i = 0; i < entry.switchOptions.size(); ++i) {
                if (i > 0) out += ", ";
                out += std::to_string(entry.switchOptions[i]);
            }
            out += ";\r\n";
        }
        out += VecLine("Pos", entry.pos);
        out += VecLine("Dir", entry.dir);
        out += VecLine("Up", entry.up);
        out += "\t}\r\n";
    }
    out += "}\r\n";

    result.ok = true;
    return result;
}

}  // namespace rs::legacy_layout

#include "SceneryTemplateSchema.h"

#include "PiValueAccessors.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiBlock;

// 種類ごとの違い(箱・個体・プラグインのキー名)は表で持つ。
struct KindNames {
    const char* box;
    const char* child;
    const char* pluginKey;
};

constexpr KindNames kStationNames{"StationBuildTemplate", "StationTemplate", "StationPlugin"};
constexpr KindNames kStructNames{"StructBuildTemplate", "StructTemplate", "StructPlugin"};

SceneryTemplateEntry ParseEntry(const PiBlock& block, const KindNames& names) {
    SceneryTemplateEntry entry;
    entry.plugin = GetString(block, names.pluginKey);
    entry.switchOptions = GetIntArray(block, "SwitchOption");
    entry.pos = GetVec3(block, "Pos");
    entry.dir = GetVec3(block, "Dir");
    if (block.Find("Up")) entry.up = GetVec3(block, "Up");
    return entry;
}

}  // namespace

SceneryTemplateDef ParseSceneryTemplate(const rs::legacy_pi::PiDocument& doc,
                                        std::vector<std::string>& warnings) {
    SceneryTemplateDef out;

    const PiBlock* header = nullptr;
    const PiBlock* box = nullptr;
    const KindNames* names = nullptr;
    for (const auto& n : doc.nodes) {
        if (n.name == "DatafileHeader") {
            header = &n;
        } else if (!box && n.name == kStationNames.box) {
            box = &n;
            names = &kStationNames;
            out.kind = SceneryTemplateKind::Station;
        } else if (!box && n.name == kStructNames.box) {
            box = &n;
            names = &kStructNames;
            out.kind = SceneryTemplateKind::Struct;
        }
    }
    if (!header) warnings.push_back("DatafileHeader が見つかりません");
    if (!box) {
        warnings.push_back("StationBuildTemplate / StructBuildTemplate ブロックが見つかりません");
        return out;
    }

    out.found = true;
    for (const PiBlock* t : box->Children(names->child)) out.entries.push_back(ParseEntry(*t, *names));
    return out;
}

}  // namespace rs::legacy_layout

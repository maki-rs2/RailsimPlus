#include "TrainGroupTemplateSchema.h"

#include "PiValueAccessors.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiBlock;

TrainTemplateEntry ParseTrainTemplate(const PiBlock& block) {
    TrainTemplateEntry entry;
    entry.trainPlugin = GetString(block, "TrainPlugin");
    entry.reverse = GetYesNo(block, "Reverse", false);
    entry.switchOptions = GetIntArray(block, "SwitchOption");
    return entry;
}

} // namespace

TrainGroupTemplateDef ParseTrainGroupTemplate(const rs::legacy_pi::PiDocument& doc,
                                              std::vector<std::string>& warnings) {
    TrainGroupTemplateDef out;

    const PiBlock* header = nullptr;
    const PiBlock* group = nullptr;
    for (const auto& n : doc.nodes) {
        if (n.name == "DatafileHeader") header = &n;
        else if (n.name == "TrainGroupTemplate") group = &n;
    }
    if (!header) warnings.push_back("DatafileHeader が見つかりません");
    if (!group) {
        warnings.push_back("TrainGroupTemplate ブロックが見つかりません");
        return out;
    }

    out.found = true;
    for (const PiBlock* t : group->Children("TrainTemplate"))
        out.trains.push_back(ParseTrainTemplate(*t));

    return out;
}

} // namespace rs::legacy_layout

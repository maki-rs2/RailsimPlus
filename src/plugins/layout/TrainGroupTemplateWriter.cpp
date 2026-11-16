#include "TrainGroupTemplateWriter.h"

namespace rs::legacy_layout {

TemplateWriteResult WriteTrainGroupTemplate(const TrainGroupTemplateDef& def) {
    TemplateWriteResult result;
    for (const TrainTemplateEntry& train : def.trains) {
        if (!IsWritableTemplateString(train.trainPlugin)) {
            result.badField = "TrainPlugin";
            return result;
        }
    }

    std::string& out = result.text;
    out += "DatafileHeader{\r\n"
           "\tRailSimVersion = 2.15;\r\n"
           "\tDatafileType = TrainGroupTemplate;\r\n"
           "}\r\n"
           "TrainGroupTemplate{\r\n";
    for (const TrainTemplateEntry& train : def.trains) {
        out += "\tTrainTemplate{\r\n";
        out += "\t\tTrainPlugin = " + QuoteTemplateString(train.trainPlugin) + ";\r\n";
        out += std::string("\t\tReverse = ") + (train.reverse ? "yes" : "no") + ";\r\n";
        if (!train.switchOptions.empty()) {
            out += "\t\tSwitchNum = " + std::to_string(train.switchOptions.size()) + ";\r\n";
            out += "\t\tSwitchOption = ";
            for (size_t i = 0; i < train.switchOptions.size(); ++i) {
                if (i > 0) out += ", ";
                out += std::to_string(train.switchOptions[i]);
            }
            out += ";\r\n";
        }
        out += "\t}\r\n";
    }
    out += "}\r\n";

    result.ok = true;
    return result;
}

}  // namespace rs::legacy_layout

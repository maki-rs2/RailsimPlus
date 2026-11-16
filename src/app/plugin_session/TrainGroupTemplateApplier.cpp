#include "TrainGroupTemplateApplier.h"

#include <utility>

#include "FormationBuilder.h"
#include "PluginSelector.h"
#include "TrainGroupTemplateParse.h"

namespace rs::app {

TrainGroupTemplateApplyResult ApplyTrainGroupTemplateFile(const std::string& templatePathUtf8,
                                                          PluginSelector& selector,
                                                          const std::string& pluginRootUtf8) {
    TrainGroupTemplateApplyResult result;

    TrainGroupTemplateParseResult parsed = ParseTrainGroupTemplateFile(templatePathUtf8, pluginRootUtf8);
    result.warnings = std::move(parsed.warnings);
    if (!parsed.ok) return result;
    // 別の形式のファイルや空のテンプレートで、空の編成だけが増えないようにする。
    if (parsed.cars.empty()) {
        result.warnings.push_back("TrainTemplateが1つもありません: " + templatePathUtf8);
        return result;
    }

    result.addedCount = BuildFormationFromPlugins(selector, parsed.formationName, parsed.cars,
                                                  parsed.installRootUtf8, result.warnings);

    result.ok = true;
    return result;
}

} // namespace rs::app

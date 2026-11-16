#include "TrainGroupTemplateParse.h"

#include <filesystem>
#include <system_error>

#include "InstallRoot.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/layout/TrainGroupTemplateSchema.h"
#include "../../plugins/schema/parser/Parser.h"

namespace rs::app {

TrainGroupTemplateParseResult ParseTrainGroupTemplateFile(const std::string& templatePathUtf8,
                                                          const std::string& pluginRootUtf8) {
    TrainGroupTemplateParseResult result;

    const std::wstring path = rs::platform_text::Utf8ToWide(templatePathUtf8);
    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(path, diag);
    if (diag.HasErrors()) {
        result.warnings.push_back("PIパース失敗: " + templatePathUtf8);
        return result;
    }

    std::vector<std::string> schemaWarnings;
    rs::legacy_layout::TrainGroupTemplateDef def =
        rs::legacy_layout::ParseTrainGroupTemplate(doc, schemaWarnings);
    result.warnings.insert(result.warnings.end(), schemaWarnings.begin(), schemaWarnings.end());

    if (!def.found) return result;  // 別の形式のファイル。okにしない

    result.installRootUtf8 = pluginRootUtf8.empty()
        ? rs::platform_text::WideToUtf8(ResolveInstallRootFromFilePath(path).wstring())
        : pluginRootUtf8;
    result.formationName =
        rs::platform_text::WideToUtf8(std::filesystem::path(path).stem().wstring());

    result.cars.reserve(def.trains.size());
    for (const rs::legacy_layout::TrainTemplateEntry& t : def.trains)
        result.cars.push_back(FormationCarSpec{t.trainPlugin, t.reverse, t.switchOptions});

    result.ok = true;
    return result;
}

TrainGroupTemplateSummary SummarizeTrainGroupTemplate(const TrainGroupTemplateParseResult& parsed) {
    TrainGroupTemplateSummary summary;
    summary.carCount = parsed.cars.size();
    for (const FormationCarSpec& spec : parsed.cars) {
        const std::string folder = ResolveTrainCarFolder(parsed.installRootUtf8, spec);
        std::error_code error;
        if (folder.empty() ||
            !std::filesystem::is_directory(rs::platform_text::Utf8ToWide(folder), error)) {
            ++summary.missingCount;
        }
    }
    return summary;
}

std::string ResolveTrainCarFolder(const std::string& installRootUtf8, const FormationCarSpec& spec) {
    return rs::legacy_layout::ResolveTrackPluginFolder(installRootUtf8, "Train", spec.trainPlugin);
}

} // namespace rs::app

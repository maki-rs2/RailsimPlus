#include "RailwayPluginSetResolve.h"

#include <filesystem>

#include "InstallRoot.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/RailwayPluginSetSchema.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/schema/parser/Parser.h"

namespace rs::app {

namespace {

// Check=yesの要素だけをfoldersへ積む。ResolveTrackPluginFolder(Rs2Import.h)は
// RailWayの参照解決と同じ「installRoot/種別フォルダ/プラグイン名」規約を使うため
// そのまま流用する。
void TryResolve(const std::string& installRootUtf8, const char* kindDir, const std::string& name, bool check,
                RailwayPluginSetResolveResult& result) {
    if (!check || name.empty()) return;
    const std::string folder = rs::legacy_layout::ResolveTrackPluginFolder(installRootUtf8, kindDir, name);
    if (folder.empty()) return;
    result.folders.push_back(folder);
}

} // namespace

RailwayPluginSetResolveResult ResolveRailwayPluginSetFile(const std::string& setPathUtf8,
                                                          const std::string& pluginRootUtf8) {
    RailwayPluginSetResolveResult result;

    const std::wstring path = rs::platform_text::Utf8ToWide(setPathUtf8);
    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(path, diag);
    if (diag.HasErrors()) {
        result.warnings.push_back("PIパース失敗: " + setPathUtf8);
        return result;
    }

    std::vector<std::string> schemaWarnings;
    const rs::legacy_layout::RailwayPluginSetDef def = rs::legacy_layout::ParseRailwayPluginSet(doc, schemaWarnings);
    result.warnings.insert(result.warnings.end(), schemaWarnings.begin(), schemaWarnings.end());
    if (!def.found) return result;  // 別の形式のファイル。okにしない
    result.settingName = def.settingName;

    const std::string installRootUtf8 = pluginRootUtf8.empty()
        ? rs::platform_text::WideToUtf8(ResolveInstallRootFromFilePath(path).wstring())
        : pluginRootUtf8;

    TryResolve(installRootUtf8, "Rail", def.railPlugin, def.railCheck, result);
    TryResolve(installRootUtf8, "Tie", def.tiePlugin, def.tieCheck, result);
    TryResolve(installRootUtf8, "Girder", def.girderPlugin, def.girderCheck, result);
    TryResolve(installRootUtf8, "Pier", def.pierPlugin, def.pierCheck, result);
    TryResolve(installRootUtf8, "Line", def.linePlugin, def.lineCheck, result);
    TryResolve(installRootUtf8, "Pole", def.polePlugin, def.poleCheck, result);

    result.ok = true;
    return result;
}

} // namespace rs::app

//
// CompatScan.cpp - CompatScan.h の実装
//
#include "CompatScan.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "CompatReport.h"
#include "PluginTools.h"
#include "../plugin_session/InstallRoot.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/layout/Rs2Schema.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/schema/parser/Parser.h"

namespace rs::app {

namespace {

// 互換の対象にする種別フォルダ。Skin は非目標なので走査しない(REQUIREMENTS §3.1)。
constexpr const wchar_t* kPluginKinds[] = {L"Rail",  L"Tie",     L"Girder",  L"Pier", L"Pole", L"Line",
                                           L"Train", L"Station", L"Struct", L"Surface", L"Env"};

bool IsWarningLine(const std::string& line) {
    return line.find("[warn]") != std::string::npos || line.find("[err") != std::string::npos;
}

// 1プラグインフォルダを読む。定義ファイルを持たない共有リソースは母数外(false)。
bool ReadPluginRecord(const std::wstring& folder, const std::string& kind, CompatRecord& out) {
    rs::legacy_plugin::LoadOptions opt;
    opt.loadMeshes = true;
    rs::legacy_plugin::LoadedPlugin lp = rs::legacy_plugin::LoadPluginFolder(folder, opt);
    if (lp.noDefinition) return false;

    out.kind = kind;
    out.path = rs::platform_text::WideToUtf8(folder);
    out.parsed = lp.ok;
    if (!lp.ok) return true;
    for (const std::string& l : lp.log)
        if (IsWarningLine(l)) out.warnings.push_back(l);
    if (out.warnings.empty()) {
        bool meshFailed = std::any_of(lp.models.begin(), lp.models.end(),
                                      [](const auto& m) { return !m.meshOk; });
        if (meshFailed) out.warnings.push_back("mesh load failed");
    }
    return true;
}

CompatRecord ReadRs2Record(const std::filesystem::path& file) {
    CompatRecord rec;
    rec.kind = kCompatKindRs2;
    rec.path = rs::platform_text::WideToUtf8(file.wstring());

    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(file.wstring(), diag);
    for (const auto& d : diag.Items())
        if (d.severity == rs::legacy_pi::Severity::Warning) rec.warnings.push_back(d.message);
    if (diag.HasErrors()) return rec;

    std::vector<std::string> warnings;
    rs::legacy_layout::Rs2Layout layout = rs::legacy_layout::ParseRs2Layout(doc, warnings);
    if (layout.scenes.empty()) return rec;

    const std::string installRoot =
        rs::platform_text::WideToUtf8(ResolveInstallRootFromFilePath(file).wstring());
    for (const auto& scene : layout.scenes) {
        rs::legacy_layout::ImportTrackGraph(scene, installRoot, warnings);
        rs::legacy_layout::ImportPlacements(scene, installRoot, warnings);
    }
    rec.parsed = true;
    rec.warnings.insert(rec.warnings.end(), warnings.begin(), warnings.end());
    return rec;
}

std::vector<std::filesystem::path> FindRs2Files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::wstring ext = it->path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".rs2") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

int RunCompatTest(const std::string& rootUtf8) {
    const std::wstring root = rs::platform_text::Utf8ToWide(rootUtf8);
    std::printf("root: %s\n", rootUtf8.c_str());

    std::vector<CompatRecord> records;
    int skippedNonPlugin = 0;
    for (const wchar_t* kind : kPluginKinds) {
        const std::string kindName = rs::platform_text::WideToUtf8(kind);
        for (const std::wstring& folder : ListSubdirs(root + L"\\" + kind)) {
            CompatRecord rec;
            if (ReadPluginRecord(folder, kindName, rec))
                records.push_back(std::move(rec));
            else
                ++skippedNonPlugin;
        }
    }
    for (const std::filesystem::path& file : FindRs2Files(root)) records.push_back(ReadRs2Record(file));

    const CompatSummary summary = AggregateCompat(records);
    const std::vector<CompatJudgement> judgements = JudgeCompat(summary);
    std::printf("%s", FormatCompatReport(summary, judgements).c_str());
    std::printf("(non-plugin dirs skipped: %d)\n", skippedNonPlugin);
    return CompatPassed(judgements) ? 0 : 1;
}

}  // namespace rs::app

#include "TemplateSaver.h"

#include <algorithm>
#include <cwctype>

#include <spdlog/spdlog.h>

#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/RailwayPluginSetWriter.h"
#include "../../plugins/layout/SceneryTemplateWriter.h"
#include "../../plugins/layout/TrainGroupTemplateWriter.h"

namespace rs::app {

namespace {

constexpr const char* kFolderNames[] = {"RailwayPluginSet", "TrainGroupTemplate", "StationBuildTemplate",
                                        "StructBuildTemplate"};

const char* FolderName(TemplateKind kind) { return kFolderNames[static_cast<int>(kind)]; }

// ファイル名の幹として使えるか。Windowsで使えない文字・末尾の空白とドット・予約名の
// パス要素(".", "..")を弾く。CP932で表せるかは本文の検査とは別にここでも見る。
bool IsUsableTemplateName(const std::string& nameUtf8) {
    if (nameUtf8.empty() || nameUtf8.back() == ' ' || nameUtf8.back() == '.') return false;
    if (nameUtf8.find_first_of("\\/:*?\"<>|") != std::string::npos) return false;
    return rs::legacy_layout::IsWritableTemplateString(nameUtf8);
}

TemplateSaveResult WriteText(const std::string& pluginRootUtf8, TemplateKind kind,
                             const std::string& nameUtf8,
                             const rs::legacy_layout::TemplateWriteResult& text) {
    TemplateSaveResult result;
    result.path = TemplateSavePath(pluginRootUtf8, kind, nameUtf8);
    if (result.path.empty()) {
        result.error = "The template name is empty or contains characters that cannot be used.";
        result.badField = "Name";
        return result;
    }
    if (!text.ok) {
        result.error = "A value cannot be written in CP932: " + text.badField;
        result.badField = text.badField;
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(result.path.parent_path(), ec);
    if (ec) {
        result.error = "Could not create the template folder: " + ec.message();
        return result;
    }
    if (!rs::platform_file::WriteFileAtomically(result.path,
                                                rs::platform_text::Utf8ToCp932(text.text),
                                                result.error)) {
        spdlog::warn("[template] failed to save {}: {}",
                     rs::platform_text::WideToUtf8(result.path.wstring()), result.error);
        return result;
    }
    spdlog::info("[template] saved {}", rs::platform_text::WideToUtf8(result.path.wstring()));
    result.ok = true;
    return result;
}

}  // namespace

std::filesystem::path TemplateSavePath(const std::string& pluginRootUtf8, TemplateKind kind,
                                       const std::string& nameUtf8) {
    if (pluginRootUtf8.empty() || !IsUsableTemplateName(nameUtf8)) return {};
    return std::filesystem::path(rs::platform_text::Utf8ToWide(pluginRootUtf8)) /
           FolderName(kind) / std::filesystem::path(rs::platform_text::Utf8ToWide(nameUtf8 + ".txt"));
}

bool TemplateFileExists(const std::string& pluginRootUtf8, TemplateKind kind,
                        const std::string& nameUtf8) {
    const std::filesystem::path path = TemplateSavePath(pluginRootUtf8, kind, nameUtf8);
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec);
}

std::vector<std::string> ListTemplateNames(const std::string& pluginRootUtf8, TemplateKind kind) {
    std::vector<std::string> names;
    if (pluginRootUtf8.empty()) return names;
    const std::filesystem::path folder =
        std::filesystem::path(rs::platform_text::Utf8ToWide(pluginRootUtf8)) / FolderName(kind);
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code statusEc;
        if (!it->is_regular_file(statusEc)) continue;
        const std::filesystem::path& path = it->path();
        std::wstring extension = path.extension().wstring();
        std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (extension != L".txt") continue;
        names.push_back(rs::platform_text::WideToUtf8(path.stem().wstring()));
    }
    std::ranges::sort(names);
    return names;
}

bool RailwayPluginSetNeedsExtraWarning(const rs::legacy_layout::RailwayPluginSetDef& def) {
    return rs::legacy_layout::HasExtraPlugins(def);
}

TemplateSaveResult SaveRailwayPluginSetTemplate(const std::string& pluginRootUtf8,
                                                const std::string& nameUtf8,
                                                const rs::legacy_layout::RailwayPluginSetDef& def) {
    return WriteText(pluginRootUtf8, TemplateKind::RailwayPluginSet, nameUtf8,
                     rs::legacy_layout::WriteRailwayPluginSet(def));
}

TemplateSaveResult SaveTrainGroupTemplate(const std::string& pluginRootUtf8,
                                          const std::string& nameUtf8,
                                          const rs::legacy_layout::TrainGroupTemplateDef& def) {
    return WriteText(pluginRootUtf8, TemplateKind::TrainGroupTemplate, nameUtf8,
                     rs::legacy_layout::WriteTrainGroupTemplate(def));
}

TemplateSaveResult SaveSceneryTemplate(const std::string& pluginRootUtf8,
                                       const std::string& nameUtf8,
                                       const rs::legacy_layout::SceneryTemplateDef& def) {
    const TemplateKind kind = def.kind == rs::legacy_layout::SceneryTemplateKind::Station
                                  ? TemplateKind::StationBuildTemplate
                                  : TemplateKind::StructBuildTemplate;
    return WriteText(pluginRootUtf8, kind, nameUtf8, rs::legacy_layout::WriteSceneryTemplate(def));
}

}  // namespace rs::app

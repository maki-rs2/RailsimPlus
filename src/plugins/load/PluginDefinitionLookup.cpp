#include "PluginDefinitionLookup.h"

#include "PluginPaths.h"
#include "../schema/PluginSchema.h"
#include "../schema/parser/Parser.h"
#include "../../platform/text/Encoding.h"

namespace rs::legacy_plugin {
rs::legacy_pi::ParsedDefinitionResult ParsePluginDefinitionFile(const std::wstring& defFile) {
    return rs::legacy_pi::GetPluginDefinitionCache().Get(defFile, [&defFile]() {
        rs::legacy_pi::ParsedDefinitionResult result;
        rs::legacy_pi::Diagnostics diagnostics;
        rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(defFile, diagnostics);
        result.diagItems = diagnostics.Items();
        if (doc.nodes.empty()) return result;
        result.def = rs::legacy_pi::BuildPluginDefinition(std::move(doc));
        result.ok = true;
        return result;
    });
}

rs::legacy_pi::ParsedDefinitionResult LoadPluginDefinition(const std::string& folderUtf8) {
    const std::vector<std::wstring> definitionFiles = FindDefinitionFiles(rs::platform_text::Utf8ToWide(folderUtf8));
    if (definitionFiles.empty()) return {};
    return ParsePluginDefinitionFile(definitionFiles.front());
}
} // namespace rs::legacy_plugin

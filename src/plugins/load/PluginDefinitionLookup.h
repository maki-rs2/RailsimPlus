#pragma once

#include <string>

#include "../schema/PluginDefinitionCache.h"

namespace rs::legacy_plugin {
rs::legacy_pi::ParsedDefinitionResult LoadPluginDefinition(const std::string& folderUtf8);
rs::legacy_pi::ParsedDefinitionResult ParsePluginDefinitionFile(const std::wstring& defFile);
} // namespace rs::legacy_plugin

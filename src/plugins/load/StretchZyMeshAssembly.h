#pragma once

#include <string>
#include <vector>

#include "../../render/mesh/Mesh.h"

namespace rs::legacy_pi { struct PluginDefinition; }

namespace rs::legacy_plugin {

struct StretchZyAssemblyPart {
    rs::render::Mesh mesh;
    int anchorPartDefIndex = -1;
    int defIndex = -1;
};

// StretchZYごとに第1取り付け先のローカル座標で作り、材質のパスを読み込み時に解決する。
std::vector<StretchZyAssemblyPart> BuildStretchZyAssemblyMesh(const rs::legacy_pi::PluginDefinition& def,
    const std::wstring& baseDir, std::vector<std::string>& log, int& missing);

} // namespace rs::legacy_plugin

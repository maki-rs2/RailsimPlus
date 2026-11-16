//
// ProfileMaterial.cpp - ProfileMaterial.h の実装
//
#include "ProfileMaterial.h"

#include "PluginPaths.h"
#include "../../platform/text/Encoding.h"

namespace rs::legacy_plugin {

namespace {
constexpr uint8_t kOpaqueAlpha = 255;
}

void CopyProfileVertexColor(rs::render::MeshVertex& meshVertex, const rs::legacy_pi::RailProfileVertex& profileVertex) {
    for (int i = 0; i < 4; ++i) meshVertex.color[i] = profileVertex.color[i];
}

void ProfileQuadSink::AddQuad(const rs::legacy_pi::RailProfileVertex& a, const rs::legacy_pi::RailProfileVertex& b,
                              std::initializer_list<uint32_t> indices) {
    const bool translucent = a.color[3] < kOpaqueAlpha || b.color[3] < kOpaqueAlpha;
    std::vector<uint32_t>& target = translucent ? translucent_ : opaque_;
    target.insert(target.end(), indices.begin(), indices.end());
}

void ProfileQuadSink::Flush(rs::render::Mesh& mesh, uint32_t materialIndex) {
    const auto emit = [&](std::vector<uint32_t>& indices, bool vertexAlpha) {
        if (indices.empty()) return;
        rs::render::SubMesh sm;
        sm.materialIndex = materialIndex;
        sm.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        sm.indexCount = static_cast<uint32_t>(indices.size());
        sm.vertexAlpha = vertexAlpha;
        mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
        mesh.submeshes.push_back(sm);
        indices.clear();
    };
    emit(opaque_, false);
    emit(translucent_, true);
}

void ApplyProfileTextures(rs::render::Material& mat, const std::string& texFileName, bool useTexture,
                          const rs::legacy_pi::ProfileMaterialExtra& extra, const std::wstring& baseDir,
                          const char* label, std::vector<std::string>& log, int& missing) {
    mat.ambient[0] = mat.ambient[1] = mat.ambient[2] = 1.0f;
    mat.useEnvMap = extra.envMap;
    mat.forceAlphaTest = extra.alphaZeroTest;
    mat.castShadow = extra.castShadow;
    if (!useTexture || texFileName.empty()) return;
    const std::wstring baseWide = ResolvePath(baseDir, texFileName);
    if (!FileExists(baseWide)) {
        log.push_back(std::string("[plugin][warn] missing ") + label + " tex: " + texFileName);
        ++missing;
        return;
    }
    mat.texturePath = rs::platform_text::WideToUtf8(baseWide);

    if (!extra.normalTexFileName.empty()) {
        const std::wstring normalWide = ResolvePath(baseDir, extra.normalTexFileName);
        if (FileExists(normalWide)) {
            mat.normalPath = rs::platform_text::WideToUtf8(normalWide);
        } else {
            log.push_back(std::string("[plugin][warn] missing ") + label + " normal tex: " + extra.normalTexFileName);
            ++missing;
        }
    } else {
        mat.normalPath = FindPairedTexture(baseWide, L"_n");
    }
    if (!mat.normalPath.empty()) mat.useBumpMap = true;
}

} // namespace rs::legacy_plugin

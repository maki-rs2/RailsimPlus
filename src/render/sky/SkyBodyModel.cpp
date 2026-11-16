#include "../RenderPrecision.h"
#include "SkyBodyModel.h"

#include <cstring>

#include "../../platform/text/Encoding.h"
#include "../resource/XLoader.h"

namespace rs::render {

SkyBodyModel LoadSkyBodyModel(const std::string& xPathUtf8, double modelScale) {
    SkyBodyModel model;
    if (xPathUtf8.empty()) {
        model.error = "no model path";
        return model;
    }
    const rs::legacy_x::LoadResult loaded = rs::legacy_x::LoadXFileAssimp(rs::platform_text::Utf8ToWide(xPathUtf8));
    if (!loaded.ok || loaded.mesh.vertices.empty() || loaded.mesh.indices.empty() || loaded.mesh.materials.empty()) {
        model.error = "cannot load " + xPathUtf8;
        return model;
    }
    const Material& material = loaded.mesh.materials[0];
    if (material.texturePath.empty()) {
        model.error = "no texture in " + xPathUtf8;
        return model;
    }
    model.image = LoadImageRGBA(material.texturePath);
    if (!model.image.ok) {
        model.error = "cannot load texture " + material.texturePath;
        return model;
    }
    model.vertices.reserve(loaded.mesh.vertices.size() * kSkyBodyVertexFloats);
    for (const MeshVertex& vertex : loaded.mesh.vertices) {
        model.vertices.insert(model.vertices.end(), {vertex.pos[0], vertex.pos[1], vertex.pos[2], vertex.uv[0], vertex.uv[1]});
    }
    model.indices = loaded.mesh.indices;
    std::memcpy(model.diffuse, material.diffuse, sizeof(model.diffuse));
    std::memcpy(model.ambient, material.ambient, sizeof(model.ambient));
    model.scale = rs::render::ToGpuFloat(modelScale);
    model.ok = true;
    return model;
}

} // namespace rs::render

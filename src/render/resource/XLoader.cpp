//
// XLoader.cpp - assimp 経由での .x 読込み
//
#include "XLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>

#include "../../platform/text/Encoding.h"
#include "MaterialOverwrite.h"
#include "ModelManager.h"

namespace rs::legacy_x {

namespace {

constexpr unsigned int kPostProcessFlags =
    aiProcess_Triangulate | aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices |
    aiProcess_GenSmoothNormals;

// RailSim II の D3DXComputeNormals はしきい値を持たず位置一致頂点の面法線を単純平均するため、
// 角度しきい値をほぼ無効化(175°=ほぼ常にスムージング)して同挙動に寄せる。
// 既に法線を持つメッシュ(.xに焼き込み済み)は GenSmoothNormals がスキップするため上書きされない。
constexpr float kSmoothingAngleDegrees = 175.0f;

void ConfigureSmoothNormals(Assimp::Importer& importer) {
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, kSmoothingAngleDegrees);
}

std::string DirOf(const std::string& utf8Path) {
    size_t p = utf8Path.find_last_of("\\/");
    if (p == std::string::npos) return {};
    return utf8Path.substr(0, p);
}

std::string ResolvePath(const std::string& baseDir, const std::string& file) {
    if (baseDir.empty() || file.empty()) return file;
    if (file.size() >= 2 && (file[1] == ':' || (file[0] == '\\' && file[1] == '\\'))) {
        return file; // 既に絶対パス
    }
    std::string base = baseDir;
    if (base.back() != '\\' && base.back() != '/') base.push_back('\\');
    return base + file;
}

rs::render::Material ConvertMaterial(const aiMaterial* src, const std::string& baseDir) {
    rs::render::Material mat;
    aiColor4D diffuse;
    if (src->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
        mat.diffuse[0] = diffuse.r;
        mat.diffuse[1] = diffuse.g;
        mat.diffuse[2] = diffuse.b;
        mat.diffuse[3] = diffuse.a;
    }
    mat.ambient[0] = mat.diffuse[0];
    mat.ambient[1] = mat.diffuse[1];
    mat.ambient[2] = mat.diffuse[2];
    aiColor3D specular;
    if (src->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS) {
        mat.specular[0] = specular.r;
        mat.specular[1] = specular.g;
        mat.specular[2] = specular.b;
    }
    float shininess = 0.0f;
    if (src->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
        mat.power = shininess;
    }
    aiColor3D emissive;
    if (src->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        mat.emissive[0] = emissive.r;
        mat.emissive[1] = emissive.g;
        mat.emissive[2] = emissive.b;
    }
    aiString texPath;
    if (src->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
        // assimp は .x を生バイトのまま読むため、CP932 のテクスチャファイル名は
        // 未変換の生バイト列で返ってくる。他経路(ReadFileAsUtf8)と揃えてここで
        // CP932->UTF-8 化する(日本語テクスチャ名がピンク化するバグの原因)。
        std::string fn = rs::platform_text::Cp932ToUtf8(
            std::string(texPath.C_Str(), texPath.length));
        mat.name = fn;
        mat.texturePath = ResolvePath(baseDir, fn);
    }
    return mat;
}

rs::render::Mesh ConvertScene(const aiScene* scene, const std::string& baseDir,
                               std::vector<std::string>& log) {
    rs::render::Mesh result;

    for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
        result.materials.push_back(ConvertMaterial(scene->mMaterials[mi], baseDir));
    }
    if (result.materials.empty()) {
        result.materials.push_back(rs::render::Material{});
    }

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* srcMesh = scene->mMeshes[mi];
        uint32_t baseVertex = static_cast<uint32_t>(result.vertices.size());

        for (unsigned int vi = 0; vi < srcMesh->mNumVertices; ++vi) {
            rs::render::MeshVertex v;
            const aiVector3D& p = srcMesh->mVertices[vi];
            v.pos[0] = p.x;
            v.pos[1] = p.y;
            v.pos[2] = p.z;
            if (srcMesh->mNormals != nullptr) {
                const aiVector3D& n = srcMesh->mNormals[vi];
                v.normal[0] = n.x;
                v.normal[1] = n.y;
                v.normal[2] = n.z;
            }
            if (srcMesh->mTextureCoords[0] != nullptr) {
                const aiVector3D& uv = srcMesh->mTextureCoords[0][vi];
                v.uv[0] = uv.x;
                v.uv[1] = uv.y;
            }
            result.vertices.push_back(v);
        }

        rs::render::SubMesh sm;
        uint32_t matIndex = srcMesh->mMaterialIndex;
        sm.materialIndex = matIndex < result.materials.size() ? matIndex : 0;
        sm.indexOffset = static_cast<uint32_t>(result.indices.size());
        for (unsigned int fi = 0; fi < srcMesh->mNumFaces; ++fi) {
            const aiFace& face = srcMesh->mFaces[fi];
            if (face.mNumIndices != 3) continue; // Triangulate済み前提
            result.indices.push_back(baseVertex + face.mIndices[0]);
            result.indices.push_back(baseVertex + face.mIndices[1]);
            result.indices.push_back(baseVertex + face.mIndices[2]);
        }
        sm.indexCount = static_cast<uint32_t>(result.indices.size()) - sm.indexOffset;
        if (sm.indexCount > 0) result.submeshes.push_back(sm);
    }

    result.ComputeAabb();
    log.push_back("[x][assimp] verts=" + std::to_string(result.vertices.size()) +
                  " indices=" + std::to_string(result.indices.size()) +
                  " submeshes=" + std::to_string(result.submeshes.size()) +
                  " materials=" + std::to_string(result.materials.size()));
    return result;
}

LoadResult BuildResult(const aiScene* scene, const std::string& baseDir,
                        const Assimp::Importer& importer) {
    LoadResult res;
    if (scene == nullptr) {
        res.log.push_back(std::string("[x][assimp][WARN] 読込失敗: ") + importer.GetErrorString());
        return res;
    }
    res.mesh = ConvertScene(scene, baseDir, res.log);
    res.ok = !res.mesh.Empty();
    return res;
}

} // namespace

LoadResult LoadXFileAssimp(const std::wstring& path) {
    std::string utf8Path = rs::platform_text::WideToUtf8(path);
    std::string baseDir = DirOf(utf8Path);

    return rs::render::GetModelManager().Get(utf8Path, [&]() {
        Assimp::Importer importer;
        ConfigureSmoothNormals(importer);
        const aiScene* scene = importer.ReadFile(utf8Path.c_str(), kPostProcessFlags);
        LoadResult res = BuildResult(scene, baseDir, importer);
        // 材質の上書きファイルは、キャッシュに入る前に1回だけ適用する。
        if (res.ok) ApplyMaterialOverwriteFor(path, res.mesh, res.log);
        return res;
    });
}

LoadResult LoadXFromTextAssimp(const std::string& utf8, const std::string& baseDirUtf8) {
    Assimp::Importer importer;
    ConfigureSmoothNormals(importer);
    const aiScene* scene = importer.ReadFileFromMemory(utf8.data(), utf8.size(),
                                                         kPostProcessFlags, "x");
    return BuildResult(scene, baseDirUtf8, importer);
}

} // namespace rs::legacy_x

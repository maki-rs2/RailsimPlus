//
// PluginModelVariant.cpp - ChangeModel動的切替(RebuildAssembly/ApplyModelVariant)
// の実装(PluginLoader.cppから分離)
//
// PluginSession::UpdateDynamicModels が毎フレーム呼ぶ、表示中パーツの差替え
// (ApplyModelVariant)と、それに伴うTrain等の合成メッシュ再構築(RebuildAssembly)
// を担う。読込時のフォルダ走査(LoadPluginFolder)とは独立した責務。
//
#include "PluginLoader.h"

#include <cstdint>
#include <cstring>
#include <utility>

#include "ModelMeshHelpers.h"

namespace rs::legacy_plugin {

namespace {

// 各パーツのメッシュを1つに合成する。頂点に partIndex(合成後の連番)を焼き込み、
// マテリアルはパーツごとにインデックスをオフセットして再配置する。
// ワールド変換(オフセット)は焼き込まず、partIndex だけ付与する
// (実際の配置は呼び出し側が毎フレーム与える gPartWorld(パーツ行列)で行う)。
rs::render::Mesh BuildAssemblyMesh(
    const std::vector<const rs::render::Mesh*>& parts) {
    rs::render::Mesh out;
    // 視点距離のモデル切替で走行中に呼ばれるので、伸長による再確保を起こさない。
    size_t vertexTotal = 0, materialTotal = 0, submeshTotal = 0, indexTotal = 0;
    for (const rs::render::Mesh* part : parts) {
        vertexTotal += part->vertices.size();
        materialTotal += part->materials.size();
        submeshTotal += part->submeshes.size();
        for (const rs::render::SubMesh& sm : part->submeshes) indexTotal += sm.indexCount;
    }
    out.vertices.reserve(vertexTotal);
    out.materials.reserve(materialTotal);
    out.submeshes.reserve(submeshTotal);
    out.indices.resize(indexTotal);
    size_t indexCursor = 0;
    for (size_t p = 0; p < parts.size(); ++p) {
        const rs::render::Mesh& src = *parts[p];
        uint32_t vBase = static_cast<uint32_t>(out.vertices.size());
        uint32_t matBase = static_cast<uint32_t>(out.materials.size());
        out.vertices.insert(out.vertices.end(), src.vertices.begin(), src.vertices.end());
        for (size_t v = vBase; v < out.vertices.size(); ++v)
            out.vertices[v].partIndex = static_cast<uint32_t>(p);
        out.materials.insert(out.materials.end(), src.materials.begin(), src.materials.end());
        for (const rs::render::SubMesh& sm : src.submeshes) {
            rs::render::SubMesh osm = sm;
            osm.materialIndex += matBase;
            osm.indexOffset = static_cast<uint32_t>(indexCursor);
            out.submeshes.push_back(osm);
            const uint32_t* srcIndices = src.indices.data() + sm.indexOffset;
            uint32_t* dstIndices = out.indices.data() + indexCursor;
            for (uint32_t k = 0; k < sm.indexCount; ++k) dstIndices[k] = srcIndices[k] + vBase;
            indexCursor += sm.indexCount;
        }
    }
    out.ComputeAabb();
    return out;
}

} // namespace

float EffectiveModelScale(float scale) {
    return scale > 0.0f ? scale : 1.0f;
}

void RebuildAssembly(LoadedPlugin& lp) {
    // lp.models[*].partDefIndex で lp.def.parts との対応を取る（Dummy パーツ等で
    // modelFile が空の場合 models 側にエントリが無いため、位置一致は前提にしない）。
    if (lp.def.parts.empty() && lp.stretchZyParts.empty()) {
        lp.assemblyMesh = rs::render::Mesh{};
        lp.assemblyPlacements.clear();
        lp.hasAssembly = false;
        return;
    }
    std::vector<const rs::render::Mesh*> assemblyParts;
    std::vector<LoadedPlugin::PartPlacement> placements;
    assemblyParts.reserve(lp.def.parts.size() + lp.stretchZyParts.size());
    placements.reserve(lp.def.parts.size() + lp.stretchZyParts.size());
    uint32_t matBaseAccum = 0;
    for (const LoadedModel& model : lp.models) {
        if (model.partDefIndex < 0) continue; // Rail押し出し/Interval等は対象外
        if (!model.meshOk) continue; // meshOk なパーツのみ対象
        size_t i = static_cast<size_t>(model.partDefIndex);
        if (i >= lp.def.parts.size()) continue;
        const rs::legacy_pi::ModelPart& part = lp.def.parts[i];
        // BuildAssemblyMesh は assemblyParts の並び順で materials を連結する
        // ため、その時点までの累計マテリアル数が matBase になる。
        assemblyParts.push_back(&model.mesh);
        LoadedPlugin::PartPlacement pl;
        pl.offsetZ = part.offsetZ;
        pl.offsetY = part.offsetY;
        pl.turn = part.turn;
        pl.label = part.label;
        pl.partDefIndex = static_cast<int>(i);
        pl.matBase = matBaseAccum;
        // Joint3D を持つパーツは worldMat（basis + AttachCoord）を優先する
        // （Station 等の施設パーツ。JointZY 由来の offsetZ/offsetY は Train 用）。
        if (!part.joints3d.empty()) {
            pl.useJoint3D = true;
            std::memcpy(pl.worldMat, part.worldMat, sizeof(pl.worldMat));
        }
        placements.push_back(pl);
        matBaseAccum += static_cast<uint32_t>(model.mesh.materials.size());
    }
    for (const StretchZyAssemblyPart& part : lp.stretchZyParts) {
        assemblyParts.push_back(&part.mesh);
        LoadedPlugin::PartPlacement pl;
        pl.partDefIndex = -1;
        pl.anchorPartDefIndex = part.anchorPartDefIndex;
        pl.matBase = matBaseAccum;
        placements.push_back(pl);
        matBaseAccum += static_cast<uint32_t>(part.mesh.materials.size());
    }
    if (!assemblyParts.empty()) {
        lp.assemblyMesh = BuildAssemblyMesh(assemblyParts);
        lp.assemblyPlacements = std::move(placements);
        lp.hasAssembly = !lp.assemblyMesh.Empty();
    } else {
        lp.assemblyMesh = rs::render::Mesh{};
        lp.assemblyPlacements.clear();
        lp.hasAssembly = false;
    }
}

void AdoptMeshAsAssembly(LoadedPlugin& lp, rs::render::Mesh mesh) {
    lp.assemblyMesh = std::move(mesh);
    // 配置は partDefIndex=-1(def.parts 非対応)のダミー1件。空だとパーツ行列が作られず、配置の行列が
    // 反映されないため、1件は必ず積む。
    lp.assemblyPlacements.assign(1, LoadedPlugin::PartPlacement{});
    lp.hasAssembly = !lp.assemblyMesh.Empty();
}

bool ApplyModelVariant(LoadedPlugin& lp, int partDefIndex, int variantIndex, float scale) {
    if (!SetModelVariantWithoutRebuild(lp, partDefIndex, variantIndex, scale)) return false;
    RebuildAssembly(lp);
    return true;
}

bool SetModelVariantWithoutRebuild(LoadedPlugin& lp, int partDefIndex, int variantIndex, float scale) {
    if (partDefIndex < 0 || static_cast<size_t>(partDefIndex) >= lp.dynamicModels.size())
        return false;
    PartDynamicModel& dyn = lp.dynamicModels[static_cast<size_t>(partDefIndex)];
    if (!dyn.variants) return false; // ChangeModel 対象外のパーツ
    if (dyn.currentVariantIndex == variantIndex && dyn.currentScale == scale)
        return false; // 変化無し

    const ModelVariant* src = nullptr;
    if (variantIndex < 0) {
        src = &dyn.variants->baseVariant;
    } else if (static_cast<size_t>(variantIndex) < dyn.variants->changeModelVariants.size()) {
        src = &dyn.variants->changeModelVariants[static_cast<size_t>(variantIndex)];
    } else {
        return false; // 不正な添字
    }

    // lp.models[] から該当パーツを探す（ChangeModel 対象は ModelPart 由来の
    // ため、partDefIndex に一致するエントリはたかだか1件）。
    LoadedModel* target = nullptr;
    for (LoadedModel& m : lp.models)
        if (m.partDefIndex == partDefIndex) { target = &m; break; }
    if (!target) return false;

    // 空指定(ChangeModel = "", …;)または読込失敗した候補は「非表示」にする
    // （RailSim II 準拠: 空モデル選択でパーツを消す）。
    // メッシュを空にし meshOk=false とすると RebuildAssembly が対象外にするため
    // assembly から除外され、単体表示でも空メッシュで非描画になる。
    if (!src->ok) {
        target->mesh = rs::render::Mesh{};
        target->meshOk = false;
        dyn.currentVariantIndex = variantIndex;
        dyn.currentScale = scale;
        return true;
    }

    target->mesh = src->meshUnscaled; // コピー(元のvariantは保持し続ける必要がある)
    BakeModelScale(target->mesh, EffectiveModelScale(scale));
    target->mesh.ComputeAabb();
    target->baseMaterials = src->baseMaterials;
    target->resolvedChangeTextures = src->resolvedChangeTextures;
    target->meshOk = true;
    dyn.currentVariantIndex = variantIndex;
    dyn.currentScale = scale;
    return true;
}

} // namespace rs::legacy_plugin

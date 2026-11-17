#include "MaterialRuntime.h"

#include "../../../platform/EnvironmentVariable.h"
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace rs::legacy_plugin {

void PartMaterialRuntime::Build(
    const rs::legacy_pi::ModelPart& part,
    const std::vector<rs::render::Material>& baseMaterials,
    const std::vector<std::array<std::string, 3>>& resolvedTexPaths) {
    texEntries_.clear();
    colorEntries_.clear();
    alphaEntries_.clear();
    envEntries_ = part.envMaps;
    bumpEntries_ = part.bumpMaps;
    alphaZeroEntries_ = part.alphaZeroTests;
    transparentEntries_ = part.transparents;
    noCastShadowEntries_ = part.noCastShadows;
    noReceiveShadowEntries_ = part.noReceiveShadows;
    partCastShadow_ = part.castShadow;
    hasConditionalFlags_ = false;
    for (const std::vector<rs::legacy_pi::MaterialFlagCustomizer>* list :
         {&envEntries_, &bumpEntries_, &alphaZeroEntries_, &transparentEntries_,
          &noCastShadowEntries_, &noReceiveShadowEntries_})
        for (const rs::legacy_pi::MaterialFlagCustomizer& e : *list)
            if (!e.trig.chain.empty()) hasConditionalFlags_ = true;
    baseMaterials_ = baseMaterials;
    cachedGeneration_ = ~uint64_t(0); // 再Build後は必ず再評価させる(ChangeModelでのentry/baseMaterials差替対策)

    part.ForEachChangeTexture([&](size_t, const rs::legacy_pi::ChangeTextureRef& ct) {
        MaterialTexEntry e;
        e.materialId = ct.materialId;
        e.trig = ct.trig;
        texEntries_.push_back(std::move(e));
    });
    AssignTexPaths(part, resolvedTexPaths);
    for (const auto& cm : part.changeMaterials) {
        MaterialColorEntry e;
        e.materialIds = cm.materialIds;
        e.hasDiffuse = cm.hasDiffuse;
        for (int i = 0; i < 4; ++i) e.diffuse[i] = cm.diffuse[i];
        e.hasAmbient = cm.hasAmbient;
        for (int i = 0; i < 3; ++i) e.ambient[i] = cm.ambient[i];
        e.hasSpecular = cm.hasSpecular;
        for (int i = 0; i < 3; ++i) e.specular[i] = cm.specular[i];
        e.hasEmissive = cm.hasEmissive;
        for (int i = 0; i < 3; ++i) e.emissive[i] = cm.emissive[i];
        e.hasPower = cm.hasPower;
        e.power = cm.power;
        e.trig = cm.trig;
        colorEntries_.push_back(std::move(e));
    }
    for (const auto& ca : part.changeAlphas) {
        MaterialAlphaEntry e;
        e.materialId = ca.materialId;
        e.alpha = ca.alpha;
        e.isDayAlpha = ca.isDayAlpha;
        e.isNightAlpha = ca.isNightAlpha;
        e.trig = ca.trig;
        alphaEntries_.push_back(std::move(e));
    }
}

void PartMaterialRuntime::Rebase(
    const rs::legacy_pi::ModelPart& part,
    const std::vector<rs::render::Material>& baseMaterials,
    const std::vector<std::array<std::string, 3>>& resolvedTexPaths) {
    if (texEntries_.size() != part.ChangeTextureCount()) {
        Build(part, baseMaterials, resolvedTexPaths);
        return;
    }
    baseMaterials_ = baseMaterials;
    cachedGeneration_ = ~uint64_t(0); // Buildと同じく、差し替え後は必ず再評価させる
    AssignTexPaths(part, resolvedTexPaths);
}

void PartMaterialRuntime::AssignTexPaths(
    const rs::legacy_pi::ModelPart& part,
    const std::vector<std::array<std::string, 3>>& resolvedTexPaths) {
    const bool useResolved = resolvedTexPaths.size() == part.ChangeTextureCount();
    part.ForEachChangeTexture([&](size_t i, const rs::legacy_pi::ChangeTextureRef& ct) {
        if (i >= texEntries_.size()) return;
        MaterialTexEntry& e = texEntries_[i];
        if (useResolved) {
            e.base = resolvedTexPaths[i][0];
            e.normal = resolvedTexPaths[i][1];
            e.pbr = resolvedTexPaths[i][2];
        } else {
            e.base = ct.base;
            e.normal = ct.normal;
            e.pbr = ct.pbr;
        }
    });
}

void PartMaterialRuntime::CollectTextureCandidatesForResolver(
    const rs::legacy_pi::SwitchResolver& resolve,
    std::vector<std::array<std::string, 3>>& out) const {
    for (const MaterialTexEntry& e : texEntries_) {
        if (e.base.empty()) continue;
        if (!rs::legacy_pi::IsTriggerActive(e.trig, resolve)) continue;
        out.push_back({e.base, e.normal, e.pbr});
    }
}

void PartMaterialRuntime::CollectTextureTriggerSwitches(
    const std::vector<std::string>& candidates, std::set<std::string>& referenced) const {
    for (const MaterialTexEntry& e : texEntries_) {
        if (e.base.empty()) continue;
        rs::legacy_pi::AddReferencedSwitches(e.trig, candidates, referenced);
    }
}

void PartMaterialRuntime::ApplyTextureEntries(
    const rs::legacy_pi::SwitchResolver& resolve, const std::vector<std::string>* excludedSwitches,
    std::vector<MaterialApplyResult>& out) const {
    for (const MaterialTexEntry& e : texEntries_) {
        if (e.materialId < 0 || size_t(e.materialId) >= out.size()) continue;
        if (excludedSwitches != nullptr &&
            rs::legacy_pi::TriggerReferencesAnySwitch(e.trig, *excludedSwitches))
            continue;
        if (!rs::legacy_pi::IsTriggerActive(e.trig, resolve)) continue;
        MaterialApplyResult& r = out[size_t(e.materialId)];
        r.hasTextureOverride = true;
        r.base = e.base;
        r.normal = e.normal;
        r.pbr = e.pbr;
    }
}

std::vector<MaterialApplyResult> PartMaterialRuntime::ResolveTextureOverridesExcludingSwitches(
    const rs::legacy_pi::SwitchResolver& resolve,
    const std::vector<std::string>& excludedSwitches) const {
    std::vector<MaterialApplyResult> out(baseMaterials_.size());
    for (size_t i = 0; i < baseMaterials_.size(); ++i) {
        out[i].materialId = int(i);
        out[i].color = baseMaterials_[i];
    }
    ApplyTextureEntries(resolve, &excludedSwitches, out);
    return out;
}

std::vector<MaterialApplyResult> PartMaterialRuntime::Apply(
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t generation, float dayAlpha) const {
    if (generation == cachedGeneration_) return cachedResults_;

    std::vector<MaterialApplyResult> out(baseMaterials_.size());
    for (size_t i = 0; i < baseMaterials_.size(); ++i) {
        out[i].materialId = int(i);
        out[i].color = baseMaterials_[i];
    }
    if (out.empty()) return out;

    // 1エントリの active 判定は共有関数 IsTriggerActive に一本化
    // （chain の全ステップをAND評価。SwitchCase の Default は
    // TriggerStep::declaredCaseValues 単体で完結するため、ここで
    // switchName横断のsiblingスキャンを行う必要はない）。
    using rs::legacy_pi::IsTriggerActive;

    // --- テクスチャ: 宣言順、最後に active だったもの勝ち。
    ApplyTextureEntries(resolve, nullptr, out);

    // --- EnvMap / BumpMap / AlphaZeroTest / Transparent / NoCastShadow / NoReceiveShadow:
    // 条件が成り立つ指定に載った材質のフラグを立てる。baseMaterials_ は読込時の指定を反映する前の
    // 値なので、Always の指定も含めて全件から計算し直す。
    // castShadow の出発点はパーツ単位の CastShadow(no なら材質の指定にかかわらず影を落とさない)。
    const auto collectActive = [&](const std::vector<rs::legacy_pi::MaterialFlagCustomizer>& entries) {
        std::vector<char> active(out.size(), 0);
        for (const rs::legacy_pi::MaterialFlagCustomizer& e : entries) {
            if (!IsTriggerActive(e.trig, resolve)) continue;
            for (int mid : e.materialIds)
                if (mid >= 0 && size_t(mid) < out.size()) active[size_t(mid)] = 1;
        }
        return active;
    };
    const std::vector<char> envActive = collectActive(envEntries_);
    const std::vector<char> bumpActive = collectActive(bumpEntries_);
    const std::vector<char> alphaZeroActive = collectActive(alphaZeroEntries_);
    const std::vector<char> transparentActive = collectActive(transparentEntries_);
    const std::vector<char> noCastActive = collectActive(noCastShadowEntries_);
    const std::vector<char> noReceiveActive = collectActive(noReceiveShadowEntries_);
    for (size_t i = 0; i < out.size(); ++i) {
        rs::render::Material& m = out[i].color;
        m.useEnvMap = envActive[i] != 0;
        m.useBumpMap = bumpActive[i] != 0;
        m.forceAlphaTest = alphaZeroActive[i] != 0;
        // Transparent は影を落とさず受けない。
        m.castShadow = partCastShadow_ && !transparentActive[i] && !noCastActive[i];
        m.receiveShadow = !transparentActive[i] && !noReceiveActive[i];
    }

    // --- 色/材質: 宣言順、最後に active だったもの勝ち（項目単位で上書き、
    // has* フラグが立っていない項目はベース値のまま = 前の適用結果を保持）。
    for (const auto& e : colorEntries_) {
        if (!IsTriggerActive(e.trig, resolve)) continue;
        for (int mid : e.materialIds) {
            if (mid < 0 || size_t(mid) >= out.size()) continue;
            rs::render::Material& c = out[mid].color;
            if (e.hasDiffuse) {
                c.diffuse[0] = e.diffuse[0]; c.diffuse[1] = e.diffuse[1];
                c.diffuse[2] = e.diffuse[2]; c.diffuse[3] = e.diffuse[3];
            }
            if (e.hasAmbient) {
                c.ambient[0] = e.ambient[0]; c.ambient[1] = e.ambient[1];
                c.ambient[2] = e.ambient[2];
            }
            if (e.hasSpecular) {
                c.specular[0] = e.specular[0]; c.specular[1] = e.specular[1];
                c.specular[2] = e.specular[2];
            }
            if (e.hasEmissive) {
                c.emissive[0] = e.emissive[0]; c.emissive[1] = e.emissive[1];
                c.emissive[2] = e.emissive[2];
            }
            if (e.hasPower) c.power = e.power;
        }
    }

    // --- アルファ(ChangeAlpha): 宣言順、最後に active だったもの勝ち。
    // ChangeMaterial の Diffuse.a より後に適用するため、同一マテリアルへ
    // 両方が active な場合はこちらの値が最終的に勝つ。
    // 診断ダンプ可否はエントリ毎ではなくループ外で1回だけ読む(Applyは材質の
    // 再計算のたびに走るため)。
    const bool dumpAlpha = rs::platform::ReadEnvironmentVariable("RS_DUMP_ALPHA_CLASS") != nullptr;
    for (const auto& e : alphaEntries_) {
        if (e.materialId < 0 || size_t(e.materialId) >= out.size()) continue;
        const bool active = IsTriggerActive(e.trig, resolve);
        const float resolvedAlpha = e.isDayAlpha ? dayAlpha :
            (e.isNightAlpha ? 1.0f - dayAlpha : e.alpha);
        // ChangeAlphaが実際に発火しているか、発火時の値は幾つかを確定する。
        if (dumpAlpha) {
            spdlog::info("[alpharuntime] mat={} alpha={:.3f} triggerActive={}",
                         e.materialId, resolvedAlpha, active);
        }
        if (!active) continue;
        out[e.materialId].color.diffuse[3] = resolvedAlpha;
    }

    cachedResults_ = out;
    cachedGeneration_ = generation;
    return out;
}

} // namespace rs::legacy_plugin

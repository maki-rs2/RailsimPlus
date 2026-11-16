#include "SubmeshAlphaClassify.h"

#include "EnvFlag.h"
#include "../resource/RenderTuning.h"

namespace rs::render {

bool ParseAlphaZeroKBuild(const char* envValue) { return ParseEnvFlag(envValue, true); }

bool AlphaZeroKBuildEnabled() {
    static const bool kEnabled = ReadEnvFlag("RS_ALPHAZERO_KBUILD", true);
    return kEnabled;
}

bool AlphaZeroForcesCut(bool forceAlphaTest, bool kBuildMode) { return forceAlphaTest && !kBuildMode; }

bool AlphaZeroUsesPointSampling(bool forceAlphaTest, bool kBuildMode) { return forceAlphaTest && kBuildMode; }

SubmeshAlphaClass ClassifyAlphaClass(const AlphaClassInputs& inputs) {
    // 材質色Diffuse.a<1の材質は、RailSim II と同じ1パス(Blended)で描く。Transparent指定の材質だけは、
    // 実アルファを持つものを深度書込OFFの半透明パスへ送る。
    if (inputs.matAlphaReal) {
        return inputs.forceTransparent ? SubmeshAlphaClass::Transparent : SubmeshAlphaClass::Blended;
    }
    // ChangeAlphaだけで見え消えする材質(地の色Diffuse.a=1.0)のうち、テクスチャに穴を持たないものは
    // 不透明系の2段描画(深度専用パス→カラーパス)へ入れ、submesh順(=RailSim II の描画順に近い)で描く。
    // 深度専用パスは実行時アルファ×テクスチャalphaでclipするため、消灯中(alpha=0)は深度も色も
    // 残らない。半透明バケツ(全不透明の後に描く)のままだと、先に描かれた窓ガラス
    // (中間alpha>=閾値で深度を書く)の奥にある点灯レイヤー(車内灯等)が深度で全画素が除外される。
    if (inputs.changeAlphaOnly) {
        if (inputs.forceTransparent) return SubmeshAlphaClass::LateOpaque;
        // テクスチャに穴を持つ光のレイヤー(ブラインド、点灯レンズ周りの光の輪など)も、
        // Diffuse.aの降順に Blended の1パスで描く。半透明パスへ回すと、先に描いた窓ガラスの深度で
        // 奥のレイヤーが落ちる。
        if (inputs.texHole) return SubmeshAlphaClass::BlendedInPlace;
        return SubmeshAlphaClass::OpaqueAlphaTex;
    }
    if (inputs.forceTransparent) return SubmeshAlphaClass::LateOpaque;
    if (inputs.texHole || inputs.forceCut || inputs.swappable) return SubmeshAlphaClass::Cutout;
    if (inputs.texValid && inputs.texHasAlpha) return SubmeshAlphaClass::OpaqueAlphaTex;
    return SubmeshAlphaClass::Opaque;
}

bool NeedsSemiAlphaSplit(SubmeshAlphaClass alphaClass, bool forceCut, bool swappable, float semiAlphaFraction) {
    return alphaClass == SubmeshAlphaClass::Cutout && !forceCut && !swappable &&
           semiAlphaFraction >= RenderTuning::Embedded().semiAlphaMinFraction;
}

ShadowCasterKind ClassifyShadowCaster(const SubMesh& submesh) {
    if (!submesh.castShadow) return ShadowCasterKind::None;
    switch (submesh.alphaClass) {
    case SubmeshAlphaClass::Opaque: return ShadowCasterKind::Solid;
    // 中間のアルファを持つ不透明は、通常の描画でも穴あきと同じ深度専用パスで深度を確定させる。影も同じ判定にそろえる。
    case SubmeshAlphaClass::OpaqueAlphaTex:
    case SubmeshAlphaClass::Cutout: return ShadowCasterKind::AlphaTest;
    case SubmeshAlphaClass::LateOpaque:
    case SubmeshAlphaClass::Blended:
    case SubmeshAlphaClass::BlendedInPlace:
    case SubmeshAlphaClass::Transparent: return ShadowCasterKind::None;
    }
    return ShadowCasterKind::None;
}

unsigned CountShadowCasters(const std::vector<SubMesh>& submeshes) {
    unsigned count = 0;
    for (const SubMesh& submesh : submeshes) {
        if (ClassifyShadowCaster(submesh) != ShadowCasterKind::None) ++count;
    }
    return count;
}

ShadowCasterKind ClassifyPrepass(const SubMesh& submesh) {
    switch (submesh.alphaClass) {
    case SubmeshAlphaClass::Opaque: return ShadowCasterKind::Solid;
    case SubmeshAlphaClass::OpaqueAlphaTex:
    case SubmeshAlphaClass::Cutout: return ShadowCasterKind::AlphaTest;
    // 後描き不透明は、他の物の後に重ね描きする層(表示器のカバーガラスなど)なので、載せない。載せると、その背後の面の
    // AO が、重ね描きの層の深度で計算されてしまう。
    case SubmeshAlphaClass::LateOpaque:
    case SubmeshAlphaClass::Blended:
    case SubmeshAlphaClass::BlendedInPlace:
    case SubmeshAlphaClass::Transparent: return ShadowCasterKind::None;
    }
    return ShadowCasterKind::None;
}

unsigned CountPrepassDraws(const std::vector<SubMesh>& submeshes) {
    unsigned count = 0;
    for (const SubMesh& submesh : submeshes) {
        if (ClassifyPrepass(submesh) != ShadowCasterKind::None) ++count;
    }
    return count;
}

const char* SubmeshAlphaClassName(SubmeshAlphaClass alphaClass) {
    switch (alphaClass) {
    case SubmeshAlphaClass::Opaque: return "Opaque";
    case SubmeshAlphaClass::OpaqueAlphaTex: return "OpaqueAlphaTex";
    case SubmeshAlphaClass::Cutout: return "Cutout";
    case SubmeshAlphaClass::LateOpaque: return "LateOpaque";
    case SubmeshAlphaClass::Blended: return "Blended";
    case SubmeshAlphaClass::BlendedInPlace: return "BlendedInPlace";
    case SubmeshAlphaClass::Transparent: return "Transparent";
    }
    return "Unknown";
}

} // namespace rs::render



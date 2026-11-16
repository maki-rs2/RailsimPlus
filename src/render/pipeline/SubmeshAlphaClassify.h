#pragma once

#include <vector>

#include "../mesh/Mesh.h"

namespace rs::render {

struct AlphaClassInputs {
    bool matAlpha = false;
    bool alphaOverride = false;
    bool forceTransparent = false;
    bool forceCut = false;
    bool swappable = false;
    bool texValid = false;
    bool texHole = false;
    bool texHasAlpha = false;
    bool matAlphaReal = false;
    bool changeAlphaOnly = false;
};

SubmeshAlphaClass ClassifyAlphaClass(const AlphaClassInputs& inputs);

// 穴あき(Cutout)の材質を、不透明と半透明の2つのsubmeshに分けて描くか。
// semiAlphaFraction はテクスチャのalpha>0のtexelのうち半透明の割合。AlphaZeroTest指定と
// 実行時差し替えの材質は、作者が二値の抜きと決めている/テクスチャが変わるので分けない。
bool NeedsSemiAlphaSplit(SubmeshAlphaClass alphaClass, bool forceCut, bool swappable, float semiAlphaFraction);

// AlphaZeroTest の扱いの切替。既定は RailSim II -k-build 方式(切り抜きにせず、その材質のテクスチャをフィルタ・ミップなしで読む)。
// 環境変数 RS_ALPHAZERO_KBUILD が "0" のときは二値の切り抜き。
// 環境変数の値(未設定は nullptr)から切替を決める。
bool ParseAlphaZeroKBuild(const char* envValue);
// 起動後に1回だけ環境変数を読んで返す。
bool AlphaZeroKBuildEnabled();
// 材質の AlphaZeroTest 指定を、切り抜きの根拠として使うか。
bool AlphaZeroForcesCut(bool forceAlphaTest, bool kBuildMode);
// 材質のテクスチャをフィルタ・ミップなしで読むか。
bool AlphaZeroUsesPointSampling(bool forceAlphaTest, bool kBuildMode);

// シャドウパスでの submesh の扱い。振り分けは通常の描画と同じ分類(SubmeshAlphaClass)から決める。
enum class ShadowCasterKind {
    None,      // 影を落とさない(半透明のガラスや、後描きの材質)
    Solid,     // ピクセルシェーダなしで深度だけ書く
    AlphaTest, // 穴あきのテクスチャのアルファ判定をしてから深度を書く
};
ShadowCasterKind ClassifyShadowCaster(const SubMesh& submesh);
// submesh のうち影を落とすものの数。
unsigned CountShadowCasters(const std::vector<SubMesh>& submeshes);
// 主視点の深度プリパスでの submesh の扱い(種類は影と共用。Solid/AlphaTest は同じ深度専用の描き方)。
// 先に描く不透明系(Opaque・OpaqueAlphaTex・Cutout)が対象で、他の物の後に重ね描きする層(窓ガラス Blended・
// 後描き不透明 LateOpaque・下地と同じ面に重ねる BlendedInPlace)と半透明は含めない。影と違い castShadow は見ない。
ShadowCasterKind ClassifyPrepass(const SubMesh& submesh);
// submesh のうち深度プリパスで描くものの数。
unsigned CountPrepassDraws(const std::vector<SubMesh>& submeshes);
const char* SubmeshAlphaClassName(SubmeshAlphaClass alphaClass);

} // namespace rs::render

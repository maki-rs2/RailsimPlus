//
// MeshConstants.h - メッシュのシェーダへ渡す定数の並びと、大気遠近・高さフォグの詰め方。
// 環境(EnvironmentState)・調整値(RenderTuning)を読む詰め方は MeshConstantsEnvironment.h に分ける。
//
// DX12の型に触れないので、描画なしのテストからもそのまま使える。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../sky/AtmosphereMath.h"
#include "ViewConstants.h"

namespace rs::render {

// MeshConstants に入れる材質の既定の拡散色(rgb 共通)。本編とサムネイルで共有する。
constexpr float kMeshDefaultDiffuseRgb = 0.8f;

struct alignas(256) MeshConstants {
    // world/normalMat はパーツ単位ワールド行列（StructuredBuffer gPartWorld、
    // t0 space3）に持つ。視点ごとの値(viewProj・cameraPos・APのカメラの基底)は ViewConstants(b2)にあり、
    // ここには持たない。
    float lightDir[4];    // xyz = 方向, w = dayAlpha(昼夜係数、1=昼,0=完全な夜)
    float diffuse[4];     // マテリアル拡散色
    float ambient[4];     // マテリアル環境光色
    float useTexture[4];  // x=base有無, y=normal有無, z=orm有無, w=影を受けない材質か(1=受けない)
    float material[4];    // x=カットアウト判定閾値(既定0.5) y=EnvMap指定の材質か(1=真。既定ORMの対象) z=alphaTest(1=カットアウト有効) w=アルファ乗数(既定1)
    float emissive[4];    // xyz emissive 色
    float specular[4];    // xyz=ChangeMaterial Specular色, w=Power(ChangeMaterial)
    // SetAnimation(UV型アニメ): uv'=uv*[[a,c],[b,d]]+[tx,ty]。
    // uvTransform0=(a,b,c,d) uvTransform1=(tx,ty,0,0)。既定(a=d=1,他0)=恒等。
    float uvTransform0[4];
    float uvTransform1[4];
    // Bindless方式: x=base/normal/ormの絶対ディスクリプタヒープ添字
    // (firstTexIndex_+texSlotOffset。base=+0, normal=+1, orm=+2)。シェーダは
    // bindless テーブルからこの絶対番号で参照する。
    // テクスチャ無しマテリアルは常にダミー(texSlotOffset=0)を指す。
    uint32_t texIndex[4];
    uint32_t partsPerInstance;
    uint32_t materialIndex;
    uint32_t dynamicSlotCount;
    uint32_t useInstanceMatOverride;
    // ---- ここから環境光(IBL)・太陽色(EnvironmentState)。 ----
    // ゼロ初期化のまま(iblFlags.x==0)なら、固定の環境光・白の直射光の見た目になる。
    // サムネイル撮影はこの枠を使わずゼロのまま描く。
    // x=拡散キューブ y=鏡面キューブ z=BRDF LUT w=透過率LUT のSRV絶対番号
    uint32_t iblIndex[4];
    // x=鏡面キューブのミップ数 y=拡散IBLの倍率 z=鏡面IBLの倍率 w=EnvMap既定ORMのmetallic
    float iblParams[4];
    // x=EnvMap既定ORMのroughness yzw未使用
    float iblParams2[4];
    // x=IBL有効(0/1) y=デバッグ表示モード(DebugIblMode) z=全材質PBR統一モード(0/1。環境変数 RS_PBR_UNIFIED)
    // w=前フレーム分のパーツ行列(gPartWorld)の先頭の要素番号(速度パスだけが読む。0なら前フレーム分なし)
    uint32_t iblFlags[4];
    // プラグインの空(EnvironmentState::pluginSky)。ゼロのまま(w==0)なら標準の空。
    // xyz=直射光の色(リニア) w=プラグインの空か(0/1)
    float pluginDirectional[4];
    // xyz=環境光の色(リニア。材質ambientに掛ける) w=焼き済み鏡面キューブのミップ数
    float pluginAmbient[4];
    // x=焼き済み拡散キューブ y=焼き済み鏡面キューブ z=焼き済みBRDF LUT のSRV絶対番号 w未使用
    uint32_t pluginIblIndex[4];
    // xyz=焼き済み拡散・鏡面キューブに掛けるRGB(リニア。max(Directional, Ambient)) w未使用
    float pluginMeshLightScale[4];
    // 月の直射光(EnvironmentState::moonRatio)。ゼロのまま(w==0)なら月なし。
    // xyz=観測者から見た月の方向(単位ベクトル) w=太陽の放射照度に対する月の放射照度の比
    float moonLight[4];
    // 標準の空の暗順応(EnvironmentState::adaptation)。ゼロのまま(y==0)なら暗順応なし。
    // x=暗順応の結果(1x1、x成分が倍率)のSRV絶対番号 y=有効か(0/1。真のとき直射光へ倍率を掛け、夜の床を使わない)
    // z=画面のアンビエントオクルージョン(R8)のSRV絶対番号(0=AOなし。サムネイルはゼロのまま) w=描画先の大きさ(幅 | 高さ<<16)
    uint32_t adaptation[4];
    // 影定数(b1)のGPU仮想アドレス。CPUだけが読む値で、GPUへ書かれてもシェーダは読まない。
    // 全ての描画経路(本編・インスタンスバッチ・サムネイル)はこの構造体を受け取るので、b1の張り漏れが無いように
    // ここへ載せる。0なら「影を使わない」ゼロ初期化のバッファを張る。
    uint64_t shadowConstantsAddress = 0;
    // 高さフォグ。影定数のアドレスと同じ16バイトの組の後半(HLSL は gHeightFogParams.zw を asfloat で読む)。
    // ゼロのまま(hfFlags==0)なら何も引かない(サムネイルなど)。密度・高さは apScaleAndFog.yzw。
    uint32_t hfFlags;     // 424: 高さフォグが有効か(0/1)
    float hfAlbedo;       // 428: 霧の反射率(霧に当たる光に掛ける)
    // 大気遠近(AP)。ゼロのまま(apIndex.w==0)なら何も引かない(サムネイルなど)。
    // x=APのLUTのSRV絶対番号 y=スライス数 z=タイルの大きさ(px) w=有効か(0/1)
    uint32_t apIndex[4];
    // APの明るさの倍率と高さフォグの密度。x=APの散乱光に掛ける空の明るさの倍率(標準の空は skyRadianceScale、プラグインの空は pluginSkyAtmosphereScale も掛けた値)
    // y=高さフォグの基準の高さでの密度 ρ0 [1/m] z=密度が 1/e になる高さ H [m] w=基準の高さ h0 [m]
    float apScaleAndFog[4];
    // ボリュメトリックフォグ。ゼロのまま(volumetricFogIndex.x==0)なら式の高さフォグだけを使う(サムネイルなど)。
    // x=積分の結果のSRV絶対番号 yzw未使用
    uint32_t volumetricFogIndex[4];
    // x=格子の奥行きの範囲[m] y=位相関数の偏り z=太陽の光の倍率 w未使用
    float volumetricFogParams[4];
    // ---- ここから CPU だけが読む欄(シェーダの cbuffer には宣言しない)。 ----
    // 視点の定数(ViewConstants、b2)のGPU仮想アドレス。全ての描画経路(本編・インスタンスバッチ・サムネイル・影・
    // プリパス)はこの構造体を受け取るので、b2の張り漏れが無いようにここへ載せる。0は未設定(描画しない)。
    uint64_t viewConstantsAddress = 0;
    // カメラ位置の写し。半透明の三角形を奥行き順に並べるために CPU が読む(GPU へは ViewConstants から届く)。
    float viewCameraPos[4];
};
// HLSL の cbuffer Constants(MeshShader.h)と並びを一致させる。
static_assert(offsetof(MeshConstants, useInstanceMatOverride) == 172, "HLSL gUseInstanceMatOverride の位置");
static_assert(offsetof(MeshConstants, iblIndex) == 176, "HLSL gIblIndex の位置");
static_assert(offsetof(MeshConstants, iblParams) == 192, "HLSL gIblParams の位置");
static_assert(offsetof(MeshConstants, iblParams2) == 208, "HLSL gIblParams2 の位置");
static_assert(offsetof(MeshConstants, iblFlags) == 224, "HLSL gIblFlags の位置");
static_assert(offsetof(MeshConstants, pluginDirectional) == 240, "HLSL gPluginDirectional の位置");
static_assert(offsetof(MeshConstants, pluginAmbient) == 256, "HLSL gPluginAmbient の位置");
static_assert(offsetof(MeshConstants, pluginIblIndex) == 272, "HLSL gPluginIblIndex の位置");
static_assert(offsetof(MeshConstants, pluginMeshLightScale) == 288, "HLSL gPluginMeshLightScale の位置");
static_assert(offsetof(MeshConstants, moonLight) == 304, "HLSL gMoonLight の位置");
static_assert(offsetof(MeshConstants, adaptation) == 320, "HLSL gAdaptation の位置");
static_assert(offsetof(MeshConstants, shadowConstantsAddress) == 336, "CPUだけが読む影定数のアドレス");
static_assert(offsetof(MeshConstants, hfFlags) == 344, "HLSL gHeightFogParams.z の位置(影定数のアドレスの直後)");
static_assert(offsetof(MeshConstants, hfAlbedo) == 348, "HLSL gHeightFogParams.w の位置");
static_assert(offsetof(MeshConstants, apIndex) == 352, "HLSL gApIndex の位置");
static_assert(offsetof(MeshConstants, apScaleAndFog) == 368, "HLSL gApScaleAndFog の位置");
static_assert(offsetof(MeshConstants, volumetricFogIndex) == 384, "HLSL gVolumetricFogIndex の位置");
static_assert(offsetof(MeshConstants, volumetricFogParams) == 400, "HLSL gVolumetricFogParams の位置");
static_assert(offsetof(MeshConstants, viewConstantsAddress) == 416, "CPUだけが読む視点の定数のアドレス");
static_assert(sizeof(MeshConstants) == 512, "MeshConstants must occupy two 256-byte CBV units");

// 画面のアンビエントオクルージョン(AO。R8)の結果を引くための値を詰める。AO が走ったフレームだけ呼ぶ。
// width・height は描画先(画面)の大きさ。シェーダはこの大きさでピクセルの位置を uv に直して AO を引く。
inline void ApplyAmbientOcclusionToMeshConstants(MeshConstants& dst, uint32_t srvIndex, uint32_t width,
                                                 uint32_t height) {
    dst.adaptation[2] = srvIndex;
    dst.adaptation[3] = (width & 0xFFFFu) | (height << 16);
}

// 視点の定数(ViewConstants)のうち、viewProj と cameraPos を詰める。
inline ViewConstants MakeViewConstants(const float* viewProj, const float* cameraPos) {
    ViewConstants v = {};
    std::memcpy(v.viewProj, viewProj, sizeof(v.viewProj));
    std::memcpy(v.prevViewProj, viewProj, sizeof(v.prevViewProj)); // 前フレームが無ければ「前 = 今」
    for (int i = 0; i < 3; ++i) v.cameraPos[i] = cameraPos[i];
    return v;
}

// 書き込み済みの視点の定数(GPUアドレス)を、この MeshConstants を使う全ての描画へ届ける。
// cameraPos は CPU が半透明のソートに使う写し。
inline void ApplyViewToMeshConstants(MeshConstants& dst, std::uint64_t viewConstantsAddress, const float* cameraPos) {
    dst.viewConstantsAddress = viewConstantsAddress;
    for (int i = 0; i < 3; ++i) dst.viewCameraPos[i] = cameraPos[i];
}

// 大気遠近(AP)のLUTをメッシュが引くための入力。FrameRenderer がそのフレームのLUTとカメラから作る。
struct AerialPerspectiveFrame {
    bool enabled = false;               // 偽なら MeshConstants を触らず、何も引かない
    std::uint32_t srvIndex = 0;         // LUT(アトラス)のSRV絶対番号
    int sliceCount = 0;
    int tileSize = 0;
    float maxDistanceKm = 0.0f;
    atmosphere::AerialCameraFrame camera;   // LUTを作ったときと同じカメラ
    float skyRadianceScale = 1.0f;      // 空の背景と同じ明るさにそろえる倍率(空パスの skyRadianceScale と同じ値)
};

// APのLUTの引き方を MeshConstants へ、カメラの基底を ViewConstants へ詰める。FrameRenderer が毎フレーム1か所で呼ぶ。
inline void ApplyAerialPerspectiveToMeshConstants(const AerialPerspectiveFrame& frame, MeshConstants& dst,
                                                  ViewConstants& view) {
    if (!frame.enabled) {
        dst.apIndex[3] = 0u;
        return;
    }
    dst.apIndex[0] = frame.srvIndex;
    dst.apIndex[1] = static_cast<uint32_t>(frame.sliceCount);
    dst.apIndex[2] = static_cast<uint32_t>(frame.tileSize);
    dst.apIndex[3] = 1u;
    for (int i = 0; i < 3; ++i) {
        view.apRight[i] = frame.camera.right[i];
        view.apUp[i] = frame.camera.up[i];
        view.apForward[i] = frame.camera.forward[i];
    }
    view.apRight[3] = frame.camera.tanHalfFovX;
    view.apUp[3] = frame.camera.tanHalfFovY;
    view.apForward[3] = frame.maxDistanceKm;
    dst.apScaleAndFog[0] = frame.skyRadianceScale;
}

// ボリュメトリックフォグの格子を引く値を MeshConstants へ詰める。格子を作ったフレームだけ呼ぶ。
inline void ApplyVolumetricFogToMeshConstants(MeshConstants& dst, std::uint32_t integratedSrvIndex, float maxDistance,
                                              float anisotropy, float sunIntensity) {
    dst.volumetricFogIndex[0] = integratedSrvIndex;
    dst.volumetricFogParams[0] = maxDistance;
    dst.volumetricFogParams[1] = anisotropy;
    dst.volumetricFogParams[2] = sunIntensity;
}

// 高さフォグの入力。密度・高さは RenderTuning の heightFog* と同じ意味と単位。
struct HeightFogFrame {
    bool enabled = false;          // 偽なら何も引かない
    float density = 0.0f;          // 基準の高さ h0 での密度 ρ0 [1/m]
    float heightScale = 1.0f;      // 密度が 1/e になる高さ H [m]
    float baseHeight = 0.0f;       // 基準の高さ h0 [m](ワールドの Y)
    float albedo = 1.0f;           // 霧の反射率
};

// 高さフォグを MeshConstants へ詰める(APの有効・無効とは独立)。FrameRenderer が毎フレーム1か所で呼ぶ。
inline void ApplyHeightFogToMeshConstants(const HeightFogFrame& frame, MeshConstants& dst) {
    if (!frame.enabled) {
        dst.hfFlags = 0u;
        return;
    }
    dst.hfFlags = 1u;
    dst.hfAlbedo = frame.albedo;
    dst.apScaleAndFog[1] = frame.density;
    dst.apScaleAndFog[2] = frame.heightScale;
    dst.apScaleAndFog[3] = frame.baseHeight;
}

} // namespace rs::render

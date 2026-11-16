//
// mesh_shader.h - メッシュ描画用 HLSL シェーダ文字列
//
#pragma once

#include <string>

#include "MeshBrdfShader.h"
#include "MeshFogShader.h"
#include "MeshIblShader.h"
#include "MeshShadeSurfaceShader.h"
#include "ShaderColor.h"
#include "../shadow/ShadowShader.h"
#include "../sky/ExposureShader.h"
#include "../effects/VolumetricFogCommonShader.h"

namespace rs::render {

inline const std::string kMeshShader = std::string(kColorConstantsShader) + R"(
// 視点ごとの値。C++側 ViewConstants(ViewConstants.h)と並びを一致させる。
cbuffer ViewConstants : register(b2) {
    row_major float4x4 gViewProj;
    float4 gCameraPos;
    float4 gApRight;                // xyz=カメラの右 w=tanHalfFovX
    float4 gApUp;                   // xyz=カメラの上 w=tanHalfFovY
    float4 gApForward;              // xyz=カメラの前 w=LUTの最大距離[km]
    row_major float4x4 gPrevViewProj;  // 前フレームの viewProj(ジッタなし。速度の計算が使う)
};
cbuffer Constants : register(b0) {
    // xyz=光が進む向き, w=dayAlpha(昼夜係数。1=昼、0=nightThresholdSin以下の
    // 完全な夜。rs::world::ComputeDayAlpha由来)
    float4 gLightDir;
    float4 gDiffuse;
    float4 gAmbient;
    float4 gUseTexture;   // x=base y=normal z=orm w=影を受けない材質か
    float4 gMaterial;     // x=カットアウト閾値 y=EnvMap指定の材質か z=alphaTest(カットアウト)
    float4 gEmissive;
    float4 gSpecular;     // xyz=ChangeMaterial Specular色, w=Power
    // SetAnimation(UV型アニメ): uv'=uv*[[a,c],[b,d]]+[tx,ty]。
    // gUvTransform0=(a,b,c,d) gUvTransform1=(tx,ty,*,*)。既定(a=d=1,他0)=恒等。
    float4 gUvTransform0;
    float4 gUvTransform1;
    // Bindless参照: x=base/normal/ormの絶対ディスクリプタヒープ添字
    // (base=+0, normal=+1, orm=+2)。ResourceDescriptorHeap[]で直接参照する。
    uint4 gTexIndex;
    uint gPartsPerInstance;
    uint gMaterialIndex;
    uint gDynamicSlotCount;
    uint gUseInstanceMatOverride;
    // 環境光(IBL)・太陽色(EnvironmentState)。C++側 MeshConstants の iblIndex以降と同じ並び。
    // x=拡散キューブ y=鏡面キューブ z=BRDF LUT w=透過率LUT のSRV絶対番号
    uint4 gIblIndex;
    // x=鏡面キューブのミップ数 y=拡散IBLの倍率 z=鏡面IBLの倍率 w=EnvMap既定ORMのmetallic
    float4 gIblParams;
    float4 gIblParams2;   // x=EnvMap既定ORMのroughness y=プラグインの空の拡散IBLの倍率 z=同・鏡面IBLの倍率 w=標準の空の鏡面IBLのミップの倍率
    uint4 gIblFlags;      // x=IBL有効 y=デバッグ表示モード(DebugIblMode) z=全材質PBR統一モード(0/1) w=前フレーム分の gPartWorld の先頭の要素番号(速度パスだけが読む)
    // プラグインの空(EnvironmentState::pluginSky。C++側 MeshConstants の pluginDirectional以降と同じ並び)。
    float4 gPluginDirectional;  // xyz=直射光の色(リニア) w=プラグインの空か(0/1)
    float4 gPluginAmbient;      // xyz=環境光の色(リニア) w=焼き済み鏡面キューブのミップ数
    uint4 gPluginIblIndex;      // x=焼き済み拡散 y=焼き済み鏡面 z=焼き済みBRDF LUT のSRV絶対番号
    float4 gPluginMeshLightScale;  // xyz=焼き済み拡散・鏡面キューブに掛けるRGB(リニア。max(Directional, Ambient))
    float4 gMoonLight;              // xyz=観測者から見た月の方向 w=太陽に対する月の放射照度の比(0=月なし)
    uint4 gAdaptation;              // x=暗順応の結果(1x1)のSRV絶対番号 y=標準の空の暗順応が有効か(0/1) z=画面のAO(R8)のSRV絶対番号(0=AOなし) w=描画先の大きさ(幅 | 高さ<<16)
    uint4 gHeightFogParams;         // xy=C++側の影定数のアドレス(CPUだけが読む。読まない) z=高さフォグが有効か(0/1) w=霧の反射率(asfloat)
    // 大気遠近(AP)と高さフォグ。ApplyAerialPerspective・ApplyHeightFog(MeshFogShader.h)が使う。
    uint4 gApIndex;                 // x=LUTのSRV絶対番号 y=スライス数 z=タイルの大きさ(px) w=有効か(0/1)。カメラの基底は ViewConstants(gAp*)
    // x=APの散乱光に掛ける空の明るさの倍率(標準の空は skyRadianceScale、プラグインの空は pluginSkyAtmosphereScale も掛けた値)
    // y=高さフォグの基準の高さでの密度 ρ0 [1/m] z=密度が 1/e になる高さ H [m] w=基準の高さ h0 [m]
    float4 gApScaleAndFog;
    uint4 gVolumetricFogIndex;      // x=ボリュメトリックフォグの積分の結果のSRV絶対番号(0=使わない)
    float4 gVolumetricFogParams;    // x=格子の奥行きの範囲[m] y=位相関数の偏り z=太陽の光の倍率
};
SamplerState gSmp : register(s0);
SamplerState gSmpPoint : register(s3);

// 材質のテクスチャ読み出し。pointSample が立つ材質(AlphaZeroTest のフィルタ・ミップを切る方式)は
// フィルタ・ミップなし(最近傍・最上位ミップ)で読む。フラグは材質ごとの値(定数バッファか
// インスタンスごとの材質)で、勾配を使う Sample は分岐の外に置く。
float4 SampleMaterialTex(Texture2D<float4> tex, float2 uv, bool pointSample) {
    float4 filtered = tex.Sample(gSmp, uv);
    return pointSample ? tex.SampleLevel(gSmpPoint, uv, 0) : filtered;
}

// 1パーツ分のワールド変換。C++ 側 PartXform と float4x4 単位でレイアウト一致。
struct PartXform { row_major float4x4 world; row_major float4x4 normalMat; };
StructuredBuffer<PartXform> gPartWorld : register(t0, space3);

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; uint partIndex : BLENDINDICES; float4 color : COLOR; };
// color は頂点色(sRGB の値のまま補間する。断面のメッシュだけが白以外を持つ)。
struct PSIn  { float4 pos : SV_POSITION; float3 wpos : POSITION0; float3 nrm : NORMAL; float2 uv : TEXCOORD; nointerpolation uint instanceId : TEXCOORD1; float4 color : COLOR; };

// クリップ空間の位置。深度プリパスと速度パスの深度を一致させるため、両方の頂点シェーダがこの1つの関数を通す。
float4 ProjectToClip(float4 wpos4) {
    precise float4 clip = mul(wpos4, gViewProj);
    return clip;
}

PSIn VSMainInstancedMultiPart(VSIn v, uint instanceId : SV_InstanceID) {
    PSIn o;
    PartXform px = gPartWorld[instanceId * gPartsPerInstance + v.partIndex];
    float4 wpos4 = mul(float4(v.pos, 1.0), px.world);
    o.pos = ProjectToClip(wpos4);
    o.wpos = wpos4.xyz;
    o.nrm = normalize(mul(float4(v.nrm, 0.0), px.normalMat).xyz);
    o.uv = v.uv;
    o.instanceId = instanceId;
    o.color = v.color;
    return o;
}

// 速度パス(物体の動きだけを書く)。前フレームのパーツ行列は、今の行列の gPrevPartOffset 個あとにある。
// 前の位置と今の位置を、どちらも前フレームの viewProj で射影する(カメラの動きは含めない)。
// w での割り算は補間のあと(ピクセルシェーダ)で行う。動いていないパーツは三角形を潰して描かない。
struct VelPSIn {
    float4 pos : SV_POSITION;
    float4 prevClipOld : TEXCOORD0;
    float4 prevClipNew : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint instanceId : TEXCOORD3;
};

VelPSIn VSMainVelocity(VSIn v, uint instanceId : SV_InstanceID) {
    VelPSIn o;
    uint index = instanceId * gPartsPerInstance + v.partIndex;
    PartXform px = gPartWorld[index];
    PartXform pv = gPartWorld[index + gIblFlags.w];
    float4 wpos4 = mul(float4(v.pos, 1.0), px.world);
    o.pos = ProjectToClip(wpos4);
    o.prevClipNew = mul(wpos4, gPrevViewProj);
    o.prevClipOld = mul(mul(float4(v.pos, 1.0), pv.world), gPrevViewProj);
    o.uv = v.uv;
    o.instanceId = instanceId;
    bool moving = any(px.world[0] != pv.world[0]) || any(px.world[1] != pv.world[1]) ||
                  any(px.world[2] != pv.world[2]) || any(px.world[3] != pv.world[3]);
    if (!moving) o.pos = float4(0.0, 0.0, 0.0, 0.0);
    return o;
}

float2 PSVelocity(VelPSIn i) : SV_Target {
    if (i.prevClipOld.w <= 0.0 || i.prevClipNew.w <= 0.0) return float2(0.0, 0.0);
    return i.prevClipOld.xy / i.prevClipOld.w - i.prevClipNew.xy / i.prevClipNew.w;
}

)" + kSrgbTransferShader + BuildMeshIblShader() + BuildMeshBrdfShader() + std::string(kShadowConstantsShader) + kShadowReceiveShader + kExposureShader + kMeshShadeSurfaceShader +
                                     std::string(kVolumetricFogCommonShader) + kMeshFogShader + R"(
// マテリアル1件分の定数。半透明パスとインスタンスごとの上書きで共用する。
// C++ の MatData(MeshPipelineTypes.h)と float4 単位でレイアウトを一致させる(static_assert で確かめる)。
struct MatData {
    float4 diffuse;    // rgb=拡散色, a=材質アルファ
    float4 ambient;    // rgb=環境光色
    float4 emissive;   // rgb=emissive
    float4 flags;      // x=useNormal, y=useOrm, z=useEnvMap(EnvMap指定の材質), w=影を受けない(1)
    // x=base/normal/ormの絶対ディスクリプタヒープ添字(base=+0,normal=+1,orm=+2)、
    // y=useBase, zw unused
    uint4  tex;
    float4 specular;   // xyz=ChangeMaterial Specular色, w=Power
    // SetAnimation(UV型アニメ): uv'=uv*[[a,c],[b,d]]+[tx,ty]。既定=恒等変換。
    float4 uvTransform0; // a,b,c,d
    float4 uvTransform1; // tx,ty,z=テクスチャを点サンプリングで読む(1),*
};
StructuredBuffer<MatData> gInstanceMatOverrides : register(t0, space4);
StructuredBuffer<uint> gMaterialToDynamicSlot : register(t1, space4);

// render_tuning.json の cutoutOccluderAlphaThreshold と同じ、
// cutout深度専用パスでオクルーダーとみなすサンプル後alphaの境界値。
// DXCコンパイル時の-DでC++側と同じ値を受け取る。
#ifndef KCUTOUT_OCCLUDER_ALPHA_THRESHOLD
#define KCUTOUT_OCCLUDER_ALPHA_THRESHOLD 0.95
#endif
static const float kCutoutOccluderAlphaThreshold = KCUTOUT_OCCLUDER_ALPHA_THRESHOLD;

// 穴あきの半透明側が残す alpha の下限(RenderTuning::semiAlphaLow と同じ値を-Dで受け取る)。
#ifndef KSEMI_ALPHA_LOW
#define KSEMI_ALPHA_LOW 0.02
#endif
static const float kSemiAlphaLow = KSEMI_ALPHA_LOW;

#ifdef RS_DEBUG_ORM_TINT
// 診断用(一時): 反射光が乏しい照明条件でもORM/法線テクスチャの
// 適用有無・実際のサンプル内容を判別できるよう、シェーディング結果を差し替える。
// モード1(既定): 緑=ORM適用中、赤=ORM未適用。法線は輝度で区別(明=適用中、暗=未適用)。
// モード2: 法線マップの生テクセルをそのまま表示(誤ったスロット参照の検出用。
// 正しい法線マップなら平坦部は青寄り(0.5,0.5,1)、未適用箇所はフォールバック値
// (0,0,1)の純青になる。ベース/ORM等まったく別のテクスチャが誤って読めていれば
// 色柄が全く異なって見える)。
// モード3: ORMマップの生テクセルをそのまま表示(R=AO,G=Roughness,B=Metallic)。
float3 DebugOrmTint(bool useNormal, bool useOrm) {
    float3 base = useOrm ? float3(0, 1, 0) : float3(1, 0, 0);
    return base * (useNormal ? 1.0 : 0.4);
}
#endif

// 材質色・テクスチャ・頂点色から、ベースカラーとアルファを求める。
// 頂点色は材質の Diffuse と Ambient の両方に掛かる。アルファは材質・テクスチャ・頂点色の積。
struct BaseColor {
    float3 albedo;
    float3 ambientAlbedo;
    float  alpha;
};

BaseColor ComposeBaseColor(float3 texRgb, float texAlpha, float4 diffuse, float4 ambient, float4 vertexColor) {
    BaseColor c;
    c.albedo = texRgb * LinearFromSrgb(diffuse.rgb * vertexColor.rgb);
    c.ambientAlbedo = texRgb * LinearFromSrgb(ambient.rgb * vertexColor.rgb);
    c.alpha = diffuse.a * texAlpha * vertexColor.a;
    return c;
}

float4 PSMain(PSIn i) : SV_TARGET {
    MatData overrideMat;
    bool useOverride = gUseInstanceMatOverride != 0u;
    if (useOverride) {
        uint slot = gMaterialToDynamicSlot[gMaterialIndex];
        useOverride = slot != 0xffffffffu;
        if (useOverride) overrideMat = gInstanceMatOverrides[i.instanceId * gDynamicSlotCount + slot];
    }
    float4 diffuse = useOverride ? overrideMat.diffuse : gDiffuse;
    float4 ambient = useOverride ? overrideMat.ambient : gAmbient;
    float4 emissive = useOverride ? overrideMat.emissive : gEmissive;
    float4 specular = useOverride ? overrideMat.specular : gSpecular;
    float4 uv0 = useOverride ? overrideMat.uvTransform0 : gUvTransform0;
    float4 uv1 = useOverride ? overrideMat.uvTransform1 : gUvTransform1;
    float4 flags = useOverride ? overrideMat.flags : float4(gUseTexture.y, gUseTexture.z, gMaterial.y, gUseTexture.w);
    uint4 tex = useOverride ? overrideMat.tex : uint4(gTexIndex.x, gUseTexture.x > 0.5, 0, 0);
    // SetAnimation(UV型アニメ)のUV変換。未対象マテリアルは恒等(gUvTransform0=(1,0,0,1))
    // なので tuv==i.uv のまま。
    float2 tuv = float2(i.uv.x * uv0.x + i.uv.y * uv0.z + uv1.x,
                        i.uv.x * uv0.y + i.uv.y * uv0.w + uv1.y);

    // ベースカラー
    float3 texRgb = float3(1.0, 1.0, 1.0);
    float  texAlpha = 1.0;
    if (tex.y != 0u) {
        Texture2D<float4> baseTex = ResourceDescriptorHeap[tex.x + 0];
        float4 t = SampleMaterialTex(baseTex, tuv, uv1.z > 0.5);
        texRgb = t.rgb;
        texAlpha = t.a; // 材質の半透明とテクスチャ抜きを両立
    }
    BaseColor baseColor = ComposeBaseColor(texRgb, texAlpha, diffuse, ambient, i.color);
    float3 albedo = baseColor.albedo;
    float3 ambientAlbedo = baseColor.ambientAlbedo;
    float  alpha = baseColor.alpha;
    // カットアウト（抜き）: 不透明パスで深度書き込みしたまま透明texelを破棄。
    // 閾値は gMaterial.x（既定0.5、AlphaZeroTest指定マテリアルはRailSim II互換で
    // ほぼ0＝alpha==0のみ破棄）。
    // z>1.5は穴あきの半透明側: 穴(下限未満)と、不透明側が描く上限(gMaterial.x)以上を捨てる。
    // z>0.5でxが上限のときの不透明側は、上限未満(穴と半透明)を捨てる。
    if (gMaterial.z > 1.5) {
        clip(alpha - kSemiAlphaLow);
        clip(gMaterial.x - alpha);
    } else if (gMaterial.z > 0.5) {
        clip(alpha - gMaterial.x);
    }

    float3 sampledN = float3(0, 0, 1);
    if (flags.x > 0.5) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[tex.x + 1];
        sampledN = SampleMaterialTex(normalTex, tuv, uv1.z > 0.5).xyz;
    }
    float3 sampledOrm = kDefaultOrm;
    if (flags.y > 0.5) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[tex.x + 2];
        sampledOrm = SampleMaterialTex(ormTex, tuv, uv1.z > 0.5).rgb;
    }
    // 画面のAOは、深度プリパスに載った画素にだけ使う(PSMainCutoutDepthOnly と同じ式)。アルファが閾値に満たない画素は
    // 深度を書かない(半透明に描く)ので、そこでAOを引くと背後の面の暗がりが出てしまう。
    float screenAo = 1.0;
    if (alpha >= max(kCutoutOccluderAlphaThreshold, gMaterial.x)) screenAo = SampleScreenAo(i.pos);
    SurfaceShading shading = ShadeSurface(i.wpos, i.nrm, tuv, albedo, ambientAlbedo, texRgb * emissive.rgb,
                                          sampledN, flags.x > 0.5, sampledOrm, flags.y > 0.5,
                                          specular.rgb, specular.w, flags.z > 0.5, flags.w > 0.5, screenAo);
    ApplyAerialPerspective(shading, i.wpos);
    ApplyHeightFog(shading, i.wpos);
#ifdef RS_DEBUG_ORM_TINT
    shading.specularSide = float3(0.0, 0.0, 0.0);
#if RS_DEBUG_ORM_TINT == 2
    shading.diffuseSide = sampledN;
#elif RS_DEBUG_ORM_TINT == 3
    shading.diffuseSide = sampledOrm;
#else
    shading.diffuseSide = DebugOrmTint(flags.x > 0.5, flags.y > 0.5);
#endif
#endif
    return ShadePremultiplied(shading, alpha, gMaterial.w);
}

// cutoutの深度専用パス。カラーRTのWriteMaskはPSO側で0にしているため、
// SV_TARGETを出力せず、深度を書き込むtexelだけを残す。
float CutoutDepthMargin(float2 uv, uint instanceId) {
    MatData overrideMat;
    bool useOverride = gUseInstanceMatOverride != 0u;
    if (useOverride) {
        uint slot = gMaterialToDynamicSlot[gMaterialIndex];
        useOverride = slot != 0xffffffffu;
        if (useOverride) overrideMat = gInstanceMatOverrides[instanceId * gDynamicSlotCount + slot];
    }
    float4 diffuse = useOverride ? overrideMat.diffuse : gDiffuse;
    float4 uv0 = useOverride ? overrideMat.uvTransform0 : gUvTransform0;
    float4 uv1 = useOverride ? overrideMat.uvTransform1 : gUvTransform1;
    uint4 tex = useOverride ? overrideMat.tex : uint4(gTexIndex.x, gUseTexture.x > 0.5, 0, 0);
    float2 tuv = float2(uv.x * uv0.x + uv.y * uv0.z + uv1.x,
                        uv.x * uv0.y + uv.y * uv0.w + uv1.y);
    float alpha = diffuse.a;
    if (tex.y != 0u) {
        Texture2D<float4> baseTex = ResourceDescriptorHeap[tex.x + 0];
        alpha *= SampleMaterialTex(baseTex, tuv, uv1.z > 0.5).a;
    }
    // 穴あきを不透明と半透明に分けた材質は、gMaterial.xが上限(0.95)。上限未満は深度を書かない。
    return alpha - max(kCutoutOccluderAlphaThreshold, gMaterial.x);
}

void PSMainCutoutDepthOnly(PSIn i) {
    clip(CutoutDepthMargin(i.uv, i.instanceId));
}

// 速度パスの穴あき用。深度プリパスと同じ判定で、穴の画素には速度を書かない。
float2 PSVelocityAlphaTest(VelPSIn i) : SV_Target {
    clip(CutoutDepthMargin(i.uv, i.instanceId));
    return PSVelocity(i);
}

// Scenery選択ハイライト用: テクスチャ/ライティングを
// 一切無視し、gDiffuseをそのままフラットな塗り色として返す。VSMainInstancedと
// 組み合わせ(psoInstancedHighlight_)、選択/ホバー中の1エンティティだけを
// GREATER_EQUAL深度・書込OFF・アルファブレンドで不透明描画の上から重ね塗りする
// (同一メッシュ・同一ワールド行列・同一VSブロブで再描画するため深度値は
// ビット単位で一致し、共面Zファイティングは起きない)。
float4 PSMainHighlight(PSIn i) : SV_TARGET {
    // パイプラインのブレンドはプリマルチプライ(ONE, INV_SRC_ALPHA)。
    return float4(gDiffuse.rgb * gDiffuse.a, gDiffuse.a);
}

)" R"(
// ==== 半透明パス（1ドローコール・PS内マテリアル選択） ====
// per-triangle にマテリアルとテクスチャを切り替える。run 分割・per-run の
// ルート切替を廃し、半透明三角形を1ドローで描く。テクスチャ参照は
// ResourceDescriptorHeap[]直接参照(SM6.6)で行う。


// 動的 IB の三角形順に対応する per-triangle マテリアル番号（gMats の添字）
StructuredBuffer<uint>    gTriMat : register(t0, space1);
// 全マテリアル分の定数
StructuredBuffer<MatData> gMats   : register(t1, space1);

float4 PSMainTrans(PSIn i, uint primId : SV_PrimitiveID) : SV_TARGET {
    uint matIdx = gTriMat[primId];
    MatData m = gMats[matIdx];

    uint base = m.tex.x;
    bool useBase   = m.tex.y != 0u;
    bool useNormal = m.flags.x > 0.5;
    bool useOrm    = m.flags.y > 0.5;

    // SetAnimation(UV型アニメ)のUV変換。未対象マテリアルは恒等のまま。
    float2 tuv = float2(i.uv.x * m.uvTransform0.x + i.uv.y * m.uvTransform0.z + m.uvTransform1.x,
                        i.uv.x * m.uvTransform0.y + i.uv.y * m.uvTransform0.w + m.uvTransform1.y);

    float3 texRgb = float3(1.0, 1.0, 1.0);
    float  texAlpha = 1.0;
    if (useBase) {
        Texture2D<float4> baseTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 0u)];
        float4 t = SampleMaterialTex(baseTex, tuv, gUvTransform1.z > 0.5);
        texRgb = t.rgb;
        texAlpha = t.a;
    }
    BaseColor baseColor = ComposeBaseColor(texRgb, texAlpha, m.diffuse, m.ambient, i.color);
    float3 albedo = baseColor.albedo;
    float3 ambientAlbedo = baseColor.ambientAlbedo;
    float  alpha = baseColor.alpha;

    float3 sampledN = float3(0, 0, 1);
    if (useNormal) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 1u)];
        sampledN = SampleMaterialTex(normalTex, tuv, gUvTransform1.z > 0.5).xyz;
    }
    float3 sampledOrm = kDefaultOrm;
    if (useOrm) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 2u)];
        sampledOrm = SampleMaterialTex(ormTex, tuv, gUvTransform1.z > 0.5).rgb;
    }

    SurfaceShading shading = ShadeSurface(i.wpos, i.nrm, tuv, albedo, ambientAlbedo, texRgb * m.emissive.rgb,
                                          sampledN, useNormal, sampledOrm, useOrm,
                                          m.specular.rgb, m.specular.w, m.flags.z > 0.5, gUseTexture.w > 0.5);
    ApplyAerialPerspective(shading, i.wpos);
    ApplyHeightFog(shading, i.wpos);
#ifdef RS_DEBUG_ORM_TINT
    shading.specularSide = float3(0.0, 0.0, 0.0);
#if RS_DEBUG_ORM_TINT == 2
    shading.diffuseSide = sampledN;
#elif RS_DEBUG_ORM_TINT == 3
    shading.diffuseSide = sampledOrm;
#else
    shading.diffuseSide = DebugOrmTint(useNormal, useOrm);
#endif
#endif
    return ShadePremultiplied(shading, alpha, gMaterial.w);
}

// ==== 半透明パス: Weighted Blended OIT (McGuire & Bavoil 2013) ====
// ソート不要の順序非依存透過。2つのRTへ出力する:
//   SV_Target0 accum(RGBA16F): Σ(color*a, a)*w を加算ブレンド(ONE,ONE)で蓄積
//   SV_Target1 reveal(R16F)  : Π(1-a) を乗算ブレンド(ZERO,INV_SRC_COLOR)で蓄積(1で初期化)
// 合成は WboitPass の全画面パスで avg=accum.rgb/accum.a を (1-reveal) で載せる。
// マテリアル選択は PSMainTrans と同一(gTriMat/gMats/ResourceDescriptorHeap[])。
struct PSOutWBOIT { float4 accum : SV_Target0; float reveal : SV_Target1; };

PSOutWBOIT PSMainTransWBOIT(PSIn i, uint primId : SV_PrimitiveID) {
    uint matIdx = gTriMat[primId];
    MatData m = gMats[matIdx];
    MatData overrideMat;
    bool useOverride = gUseInstanceMatOverride != 0u;
    if (useOverride) {
        uint slot = gMaterialToDynamicSlot[matIdx];
        useOverride = slot != 0xffffffffu;
        if (useOverride) overrideMat = gInstanceMatOverrides[i.instanceId * gDynamicSlotCount + slot];
    }
    float4 diffuse = useOverride ? overrideMat.diffuse : m.diffuse;
    float4 ambient = useOverride ? overrideMat.ambient : m.ambient;
    float4 emissive = useOverride ? overrideMat.emissive : m.emissive;
    float4 specular = useOverride ? overrideMat.specular : m.specular;
    float4 uv0 = useOverride ? overrideMat.uvTransform0 : m.uvTransform0;
    float4 uv1 = useOverride ? overrideMat.uvTransform1 : m.uvTransform1;
    float4 flags = useOverride ? overrideMat.flags : m.flags;
    uint4 tex = useOverride ? overrideMat.tex : m.tex;
    uint base = tex.x;
    bool useBase = tex.y != 0u;
    bool useNormal = flags.x > 0.5;
    bool useOrm = flags.y > 0.5;

    float2 tuv = float2(i.uv.x * uv0.x + i.uv.y * uv0.z + uv1.x,
                        i.uv.x * uv0.y + i.uv.y * uv0.w + uv1.y);

    float3 texRgb = float3(1.0, 1.0, 1.0);
    float texAlpha = 1.0;
    if (useBase) {
        Texture2D<float4> baseTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 0u)];
        float4 t = SampleMaterialTex(baseTex, tuv, gUvTransform1.z > 0.5);
        texRgb = t.rgb;
        texAlpha = t.a;
    }
    BaseColor baseColor = ComposeBaseColor(texRgb, texAlpha, diffuse, ambient, i.color);
    float3 albedo = baseColor.albedo;
    float3 ambientAlbedo = baseColor.ambientAlbedo;
    float alpha = baseColor.alpha;
    // gMaterial.w = アルファ乗数(既定1、配置プレビュー等が減衰させる)。重み計算・
    // accum/revealの両方に反映させるため、他の用途より前でここに乗じる。
    float surfaceAlpha = alpha; // フェード乗数を掛ける前(鏡面を弱める係数に使う)
    alpha *= gMaterial.w;
    float3 sampledN = float3(0, 0, 1);
    if (useNormal) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 1u)];
        sampledN = SampleMaterialTex(normalTex, tuv, gUvTransform1.z > 0.5).xyz;
    }
    float3 sampledOrm = kDefaultOrm;
    if (useOrm) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[NonUniformResourceIndex(base + 2u)];
        sampledOrm = SampleMaterialTex(ormTex, tuv, gUvTransform1.z > 0.5).rgb;
    }

    SurfaceShading shading = ShadeSurface(i.wpos, i.nrm, tuv, albedo, ambientAlbedo, texRgb * emissive.rgb,
                                          sampledN, useNormal, sampledOrm, useOrm,
                                          specular.rgb, specular.w, flags.z > 0.5, gUseTexture.w > 0.5);
    ApplyAerialPerspective(shading, i.wpos);
    ApplyHeightFog(shading, i.wpos);
    // プリマルチプライ: 拡散側にだけ材質・テクスチャのアルファを掛け、鏡面(映り込み)は薄めずに足す。
    // ただしアルファが小さい画素では鏡面も弱める(SpecularAlphaCoverage。白い膜の防止)。
    // alpha には上でフェード乗数を掛け済みだが、鏡面にもフェード乗数だけは掛ける(全体を薄める)。
    float3 premultiplied = shading.diffuseSide * alpha +
                           shading.specularSide * (SpecularAlphaCoverage(surfaceAlpha) * gMaterial.w);

    // 深度依存の重み(McGuire eq.9)。手前ほど重く、遠方の半透明が飽和して
    // 奥の色を潰さないようにする。厳密なビュー空間深度への置換(clip.w経由)を
    // 一度試したが、近距離のリーフ同士の重みの効き方が変わり、重なり合う
    // 半透明の混合色が全体的に薄く見えるリグレッションを
    // 招いたため、視点依存(画面端で同じ奥行きでも値が変わる)という理論上の
    // 欠点を承知の上でカメラからの直線距離に戻す。分母のスケール(200)は
    // シーンの奥行きレンジに依存する調整パラメータ(必要なら見た目を見て調整)。
    float z = length(gCameraPos.xyz - i.wpos);
    float w = alpha * clamp(0.03 / (1e-5 + pow(z / 200.0, 4.0)), 1e-2, 3e3);
    // 蓄積バッファはRGBA16F(最大65504)。近距離(重み3e3)で太陽の鏡面ハイライトのような大きな値が来ると
    // premultiplied * w が上限を超えて inf になり、合成で NaN(黒い点)になる。1画素の蓄積値が
    // kWboitAccumLimit を超えないよう、重みの方を縮める。色は avg = accum.rgb / accum.a で割り戻すので、
    // 1層だけの画素の色は変わらない(層どうしの重みの比だけが変わる)。重なる層の和にも余裕を持たせた値。
    const float kWboitAccumLimit = 16384.0;
    float accumPeak = max(max(premultiplied.r, premultiplied.g), max(premultiplied.b, alpha));
    w = min(w, kWboitAccumLimit / max(accumPeak, 1e-6));

    PSOutWBOIT o;
    o.accum  = float4(premultiplied, alpha) * w;
    o.reveal = alpha;
    return o;
}

// 不透明submeshをフェード描画するためのPS。深度プリパスと深度EQUALのカラーパスで
// 同じclip条件を使い、ピクセルごとの最前面だけを一度だけブレンドする。
float4 PSMainSolidFade(PSIn i) : SV_TARGET {
    MatData overrideMat;
    bool useOverride = gUseInstanceMatOverride != 0u;
    if (useOverride) {
        uint slot = gMaterialToDynamicSlot[gMaterialIndex];
        useOverride = slot != 0xffffffffu;
        if (useOverride) overrideMat = gInstanceMatOverrides[i.instanceId * gDynamicSlotCount + slot];
    }
    float4 diffuse = useOverride ? overrideMat.diffuse : gDiffuse;
    float4 ambient = useOverride ? overrideMat.ambient : gAmbient;
    float4 emissive = useOverride ? overrideMat.emissive : gEmissive;
    float4 specular = useOverride ? overrideMat.specular : gSpecular;
    float4 uv0 = useOverride ? overrideMat.uvTransform0 : gUvTransform0;
    float4 uv1 = useOverride ? overrideMat.uvTransform1 : gUvTransform1;
    float4 flags = useOverride ? overrideMat.flags : float4(gUseTexture.y, gUseTexture.z, gMaterial.y, gUseTexture.w);
    uint4 tex = useOverride ? overrideMat.tex : uint4(gTexIndex.x, gUseTexture.x > 0.5, 0, 0);
    float2 tuv = float2(i.uv.x * uv0.x + i.uv.y * uv0.z + uv1.x,
                        i.uv.x * uv0.y + i.uv.y * uv0.w + uv1.y);

    float3 texRgb = float3(1.0, 1.0, 1.0);
    float texAlpha = 1.0;
    if (tex.y != 0u) {
        Texture2D<float4> baseTex = ResourceDescriptorHeap[tex.x + 0];
        float4 t = SampleMaterialTex(baseTex, tuv, uv1.z > 0.5);
        texRgb = t.rgb;
        texAlpha = t.a;
    }
    BaseColor baseColor = ComposeBaseColor(texRgb, texAlpha, diffuse, ambient, i.color);
    float3 albedo = baseColor.albedo;
    float3 ambientAlbedo = baseColor.ambientAlbedo;
    float alpha = baseColor.alpha;
    if (gMaterial.z > 0.5) {
        clip(alpha - gMaterial.x);
    }
    float3 sampledN = float3(0, 0, 1);
    if (flags.x > 0.5) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[tex.x + 1];
        sampledN = SampleMaterialTex(normalTex, tuv, uv1.z > 0.5).xyz;
    }
    float3 sampledOrm = kDefaultOrm;
    if (flags.y > 0.5) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[tex.x + 2];
        sampledOrm = SampleMaterialTex(ormTex, tuv, uv1.z > 0.5).rgb;
    }

    SurfaceShading shading = ShadeSurface(i.wpos, i.nrm, tuv, albedo, ambientAlbedo, texRgb * emissive.rgb,
                                          sampledN, flags.x > 0.5, sampledOrm, flags.y > 0.5,
                                          specular.rgb, specular.w, flags.z > 0.5, flags.w > 0.5);
    ApplyAerialPerspective(shading, i.wpos);
    ApplyHeightFog(shading, i.wpos);
    // 被覆率はフェード乗数gMaterial.wだけを源にする。不透明submeshのdiffuse.a/
    // テクスチャαは不透明パスでは捨てられる値で1未満のことがあり(MeshUpload.cppの
    // opaqueHasAlphaTex)、掛けると乗数が1でも車体が透ける。抜き(cutout)判定は
    // 上のclipで材質α側に済ませてあるため、抜きが早く消えることはない。
    // ブレンドはプリマルチプライ(ONE, INV_SRC_ALPHA)。被覆率が源なので、鏡面も含めて全体にフェード乗数を掛ける。
    return ShadePremultiplied(shading, 1.0, gMaterial.w);
}
)";

} // namespace rs::render

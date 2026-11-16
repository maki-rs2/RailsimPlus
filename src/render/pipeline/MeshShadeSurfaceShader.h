//
// MeshShadeSurfaceShader.h - メッシュのライティング本体 ShadeSurface の HLSL(段ごとの関数に分けてある)
//
// MeshShader.h の cbuffer Constants・sRGB変換・MeshIblShader.h・MeshBrdfShader.h・ShadowShader.h の後ろに連結する
// (cbuffer の gLightDir/gCameraPos/gIbl* と、IBL・BRDF・影の関数を使う)。
// 不透明・インスタンス・WBOIT・フェードの各PSが共通に呼ぶ。
//
// 戻り値は「拡散側」と「鏡面側」に分ける。アルファブレンドで描くときは、材質・テクスチャのアルファを
// 拡散側にだけ掛け、鏡面(ガラスの映り込みなど)はアルファで薄めずに足す(プリマルチプライ。
// ShadePremultiplied。アルファ0の穴だけは鏡面も消す)。ブレンドしない経路は両方を足した色をそのまま使う。
//
#pragma once

namespace rs::render {

inline constexpr const char* kMeshShadeSurfaceShader = R"HLSL(
// 拡散側(環境光・発光・直射光の拡散)と鏡面側(直射光の鏡面・鏡面IBL)。ブレンドなしの色は両方の和。
struct SurfaceShading {
    float3 diffuseSide;
    float3 specularSide;
};

// プリマルチプライ済みの出力(ブレンドは ONE, INV_SRC_ALPHA)。アルファは拡散側にだけ掛け、
// フェード乗数 fade(配置プレビュー等。gMaterial.w)は鏡面も含めて全体に掛ける。出力のアルファ = alpha * fade。
// アルファが小さい画素(影の絵の縁・抜きに近い透明部分)では、鏡面もアルファに合わせて弱める。拡散だけが薄まって
// 鏡面が満額で残ると、透明に近い画素が白い膜になるため。アルファが kSpecularFullAlpha 以上ならそのまま足す
// (ガラスは材質のアルファが中間でも映り込みを残す)。アルファ0の画素は面の無い穴(RailSim II の AlphaZeroTest と同じ規則)なので0。
static const float kSpecularFullAlpha = 0.1;
float SpecularAlphaCoverage(float alpha) {
    return saturate(alpha / kSpecularFullAlpha);
}
float4 ShadePremultiplied(SurfaceShading shading, float alpha, float fade) {
    return float4((shading.diffuseSide * alpha + shading.specularSide * SpecularAlphaCoverage(alpha)) * fade, alpha * fade);
}

// 頂点法線が実際の面の向き(画面微分で求めた幾何法線)から大きくずれている画素で、鏡面を弱める係数(0〜1)。
// 0=大きくずれている、1=ずれていない。
// 植物の板や樹冠のモデルは、どの向きの葉も地面と同じ明るさに照らすため法線をわざと真上にそろえてある。
// その法線のまま鏡面を計算すると、葉が「粗い水平な床」になる。拡散はその法線のまま(作者の意図した明るさ)にし、
// 鏡面だけをこの係数で止める。幾何法線はカメラ側へ向けて比べる(両面の板)。
// しきい値: 内積 kNormalMismatchSpecularOff(約78°)以下で0、kNormalMismatchSpecularFull(60°)以上で1。
// 法線をなめらかにつないだ普通のモデルは、立方体の角でも約55°(内積0.577)までなので弱まらない。
//
// 直射の鏡面はこの係数をそのまま掛ける。低い光源の下を見ると、ハーフベクトルが真上を向いて「床」が強く光り、
// 木々の面に光源から真下へ広がる扇形の筋が出るため。
// 鏡面IBLは下限 kNormalMismatchIblSpecularFloor を残す。空を映した白い膜は弱めるが、
// 0にすると草木の反射まで消えて線路などの反射だけが目立つため。
static const float kNormalMismatchSpecularOff = 0.2;
static const float kNormalMismatchSpecularFull = 0.5;
static const float kNormalMismatchIblSpecularFloor = 0.3;
static const float kGeometricNormalMinLength = 1e-12;
float NormalMismatchSpecularScale(float3 wpos, float3 vertexNormal, float3 toCamera) {
    float3 geometric = cross(ddx(wpos), ddy(wpos));
    if (dot(geometric, geometric) <= kGeometricNormalMinLength) return 1.0;
    geometric = normalize(geometric);
    if (dot(geometric, toCamera) < 0.0) geometric = -geometric;
    return smoothstep(kNormalMismatchSpecularOff, kNormalMismatchSpecularFull, dot(vertexNormal, geometric));
}

// 法線マップの法線(ワールド)。接線フレームが作れない箇所は頂点法線のまま返す。
float3 ApplyNormalMap(float3 wpos, float2 uv, float3 n, float3 sampledNormal) {
    float3 t, b;
    if (!ComputeTangentFrame(wpos, uv, n, t, b)) return n;
    float3 nt = sampledNormal * 2.0 - 1.0;
    return normalize(mul(nt, float3x3(t, b, n)));
}

// 直射光の鏡面のスペキュラAA。画面上で隣の画素と法線が大きく違う(遠くの曲面・細かい法線マップ)と、
// つるつるの面のハイライト(幅が画素より細い)が画素に拾われたりはずれたりして、1画素の点がちらつく。
// 法線の画面上の変化量から分散を求め、GGX の alpha^2 に足してハイライトを広げる(Kaplanyan 2016 / Tokuyoshi。
// エネルギーは保たれる)。近くの平らな面は変化量が小さく、ほとんど変わらない。
// 強さ kSpecularAaStrength(DXCの-Dで render_tuning.json の specularAaStrength を受け取る)が0なら無効。
static const float kSpecularAaStrength = KSPECULAR_AA_STRENGTH;
static const float kSpecularAaVarianceScale = 0.25;
static const float kSpecularAaClampMax = 0.18;
float SpecularAaRoughness(float roughness, float3 n) {
    float3 dndx = ddx(n);
    float3 dndy = ddy(n);
    float variance = kSpecularAaVarianceScale * (dot(dndx, dndx) + dot(dndy, dndy));
    float kernelAlpha2 = min(2.0 * kSpecularAaStrength * variance, kSpecularAaClampMax);
    float alpha = roughness * roughness;
    return sqrt(sqrt(saturate(alpha * alpha + kernelAlpha2))); // alpha' = sqrt(alpha^2 + kernel)、roughness' = sqrt(alpha')
}

// ORM と材質から決まる BRDF の入力。
struct SurfaceMaterial {
    float ao;         // 環境光(拡散)に掛ける遮蔽。材質のORMのR。画面のAOがあれば ShadeSurface が合わせる
    float specularAo; // 鏡面IBLに掛ける遮蔽。通常は ao と同じ
    float roughness;
    float directRoughness; // 直射光の鏡面用(roughness にスペキュラAAを掛けた値)。IBL・多重散乱の補正は roughness を使う
    float metallic;
    bool blinnPhong;
    float3 f0;        // 本来の垂直反射率 lerp(0.04, albedo, metallic)。拡散(kd)の計算専用で、鏡面には使わない場合がある
    float3 specF0;    // 鏡面(直射光のGGX・鏡面IBL)の垂直反射率。統一モードでSpecular色を使う材質以外は f0 と同じ
    float specF90;    // 鏡面のグレージング角の反射率。specF0 が黒の材質で鏡面が残らないよう、そのとき0へ向かう。通常は1
};

// 全材質PBR統一モードか(環境変数 RS_PBR_UNIFIED。IBL有効のときだけ。IBL無効では常に偽)。
// 真のとき、Specular/Power(Blinn-Phong)の材質もGGXと鏡面IBLで描く。
bool IsPbrUnified(bool iblOn) {
    return iblOn && gIblFlags.z != 0u;
}

// Blinn-PhongのPower n から GGX の粗さへ。Blinn-Phongの分布の鋭さを GGX の alpha^2 = 2/(n+2) に対応づけ
// (alpha = sqrt(2/(n+2))、粗さ = sqrt(alpha) = (2/(n+2))^0.25)、roughness は alpha = roughness^2 の関係。
static const float kPowerToRoughnessExponent = 0.25;
static const float kPowerToAlphaSquaredNumerator = 2.0;
static const float kPowerToAlphaSquaredOffset = 2.0;
// 鏡面のF0が黒に近いとき、グレージング角の反射(F90)を0へ寄せる係数。F0がこの値の逆数(0.02)以上でF90=1。
// 実在の物質のF0は最も低くても約0.02なので、それ未満は「鏡面を持たない」とみなす。
static const float kSpecularF90FromF0Scale = 50.0;
static const float kLuminanceThird = 1.0 / 3.0;

SurfaceMaterial ResolveSurfaceMaterial(float3 albedo, float3 sampledOrm, bool useOrm, float3 specColor, float specPower,
                                       bool useEnvMap, bool iblOn) {
    SurfaceMaterial m;
    const bool unified = IsPbrUnified(iblOn);
    // 統一モードで、ORMを持たずSpecular/Powerを持つ材質は、作者の指定(Powerの鋭さ・Specular色)から粗さとF0を決める。
    // EnvMap指定でSpecular/Powerも持つ材質(車体のステンレスなど)は、RailSim II の球マップの映り込みに近づけるため、
    // Powerから粗さを決めず、専用の金属度・粗さ(KENVSPEC_*)で金属寄りに描く。
    const bool envSpec = unified && !useOrm && useEnvMap && specPower > 0.0;
    const bool specFromColor = unified && !useOrm && !envSpec && specPower > 0.0;
    // ORM パック（R=AO, G=Roughness, B=Metallic）
    m.ao = 1.0; m.roughness = KDEFAULT_ROUGHNESS; m.metallic = unified ? KDEFAULT_METALLIC : 0.0; // 既定の金属度は統一モードだけ
    if (useOrm) {
        m.ao = sampledOrm.r; m.roughness = sampledOrm.g; m.metallic = sampledOrm.b;
    } else if (envSpec) {
        m.metallic = KENVSPEC_METALLIC; m.roughness = KENVSPEC_ROUGHNESS;
    } else if (specFromColor) {
        m.roughness = KPOWER_ROUGHNESS_SCALE * pow(kPowerToAlphaSquaredNumerator / (specPower + kPowerToAlphaSquaredOffset), kPowerToRoughnessExponent);
    } else if (iblOn && useEnvMap && specPower <= 0.0) {
        // EnvMap指定でORMを持たないPBR材質の既定ORM(金属感のある反射)。ChangeMaterialのSpecular/Power指定(Blinn-Phong)の材質は作者の指定どおりにするため対象外。
        m.ao = 1.0; m.metallic = gIblParams.w; m.roughness = gIblParams2.x;
    }
    m.roughness = clamp(m.roughness, 0.04, 1.0);
    m.directRoughness = m.roughness; // 法線が決まってから ShadeSurface が SpecularAaRoughness で上書きする
    // Blinn-Phong(.xのSpecular/Power・ChangeMaterial)で鏡面を決めるか。EnvMap指定でORMを持つPBR材質は、
    // .xにPowerが付いていてもPBR(GGX・鏡面IBL)で描く(RailSim II -k-build のPBRシェーダにPowerの入力は無い)。
    m.blinnPhong = !unified && specPower > 0.0 && !(useEnvMap && useOrm);
    // F0はORM由来のmetallicのみで決める。ChangeMaterial Specularをここに混ぜない。RailSim II (D3D9固定機能,
    // D3DRS_SPECULARENABLE)のスペキュラはFresnel/F0を経由せず albedo・diffuse に無関係な加算項で、
    // specColorを一律F0へ混ぜるとdiffuse(=kd*albedo)側まで潰れてしまう(窓ガラス等specColorが強い材質で
    // 「浮いて見える/薄い部分が消える」視点依存の不具合になる)。
    m.f0 = lerp(float3(0.04, 0.04, 0.04), albedo, m.metallic);
    // 鏡面専用のF0。統一モードのSpecular/Power材質だけSpecular色(黒なら鏡面なし)。拡散側の f0 は上のまま変えない。
    // Specular色はBlinn-Phongの「ハイライトの強さ」で、反射率ではない。1.0(白)をそのままF0にすると全反射の鏡になり、
    // 鏡面IBLが空全体を映して白飛びするので、KSPECULAR_F0_SCALE を掛けた値を鏡面のF0にする。
    float3 specularF0 = specColor * KSPECULAR_F0_SCALE;
    m.specF0 = specFromColor ? specularF0 : m.f0;
    m.specF90 = specFromColor ? saturate(kSpecularF90FromF0Scale * dot(specularF0, float3(kLuminanceThird, kLuminanceThird, kLuminanceThird))) : 1.0;
    m.specularAo = m.ao;
    return m;
}

// 画面のアンビエントオクルージョン(見える割合。1=遮蔽なし)を、ピクセルの位置で引く。無効(gAdaptation.z==0)は1。
// gAdaptation.z=AOのSRV絶対番号 w=描画先の大きさ(幅 | 高さ<<16)。AOが半解像度でも、同じ画面のuvで引く。
float SampleScreenAo(float4 svPosition) {
    if (gAdaptation.z == 0u) return 1.0;
    Texture2D<float> aoTexture = ResourceDescriptorHeap[gAdaptation.z];
    float2 invSize = 1.0 / float2(gAdaptation.w & 0xFFFFu, gAdaptation.w >> 16);
    return aoTexture.SampleLevel(gSmp, svPosition.xy * invSize, 0);
}

// 拡散の遮蔽の多重反射の補正(Jimenez 2016)。反射率の高い面ほど、隙間の暗がりが周りからの反射光で浅くなる。
float3 MultiBounceAo(float ao, float3 albedo) {
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(float3(ao, ao, ao), ((ao * a + b) * ao + c) * ao);
}

// 鏡面の遮蔽(Lagarde 2014)。粗い面ほど、また視線が法線に近いほど、拡散と同じ遮蔽に近づき、
// つるつるの面では遮蔽が弱まる。
float SpecularOcclusion(float ndv, float ao, float roughness) {
    return saturate(pow(ndv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// 視線・光・法線の内積。
struct SurfaceView {
    float3 n;
    float3 v;
    float3 l;
    float ndl;
    float ndv;
    float ndh;
    float vdh;
};

// toLight: 光源へ向かう単位ベクトル(太陽は -gLightDir.xyz、月は gMoonLight.xyz)。
SurfaceView MakeSurfaceView(float3 n, float3 wpos, float3 toLight) {
    SurfaceView sv;
    sv.n = n;
    sv.l = toLight;
    sv.v = normalize(gCameraPos.xyz - wpos);
    float3 h = normalize(sv.l + sv.v);
    sv.ndl = saturate(dot(n, sv.l));
    sv.ndv = saturate(dot(n, sv.v));
    sv.ndh = saturate(dot(n, h));
    sv.vdh = saturate(dot(sv.v, h));
    return sv;
}

float3 FresnelSchlick90(float3 f0, float f90, float cosTheta) {
    return f0 + (f90 - f0) * pow(1.0 - cosTheta, 5.0);
}

float3 FresnelSchlick(float3 f0, float cosTheta) {
    return FresnelSchlick90(f0, 1.0, cosTheta);
}

// 環境マップの鏡面に使うフレネル。RailSim II -k-build のPBRシェーダに合わせ、視線と法線の角度(NdotV)から
// 粗さ込みのSchlick(F0 + (max(1-roughness, F0) - F0) * (1-NdotV)^5)を求めて、焼き済みBRDF LUTの
// F0の位置に渡す。
// f0・f90 は拡散側(m.f0)か鏡面側(m.specF0・m.specF90)かを呼び出し側が選ぶ。
float3 FresnelForIbl(SurfaceMaterial m, float3 f0, float f90, float ndv) {
    float3 glossiness = float3(f90 * (1.0 - m.roughness), f90 * (1.0 - m.roughness), f90 * (1.0 - m.roughness));
    return f0 + (max(glossiness, f0) - f0) * pow(1.0 - ndv, 5.0);
}

// 標準の空の鏡面(直射光・鏡面IBL)に掛ける多重散乱の補正。生成のBRDF LUT(縦軸=roughness)を引く。
float3 StandardSkyEnergyCompensation(SurfaceMaterial m, float ndv) {
    Texture2D<float4> brdfLut = ResourceDescriptorHeap[gIblIndex.z];
    float2 ab = brdfLut.SampleLevel(gIblSmp, float2(ndv, m.roughness), 0).xy;
    return EnergyCompensation(m.specF0, ab);
}

// 直射光の拡散(sunColor は掛けない)。太陽が沈むと直射光成分そのものを減衰させる(dayAlpha=0で完全に消す)。
// これが無いと太陽高度に関係なくL方向は下向きになり続け、下向きの面(床下・軒下等)が
// dot(N,L)>0で「下から光が当たる」ように見える。
float3 DirectDiffuse(SurfaceView sv, SurfaceMaterial m, float3 albedo, float dayAlpha) {
    float3 kd = (1.0 - FresnelSchlick(m.f0, sv.vdh)) * (1.0 - m.metallic);
    return kd * albedo * sv.ndl * dayAlpha;
}

// 直射光の鏡面(sunColor は掛けない)。
float3 DirectSpecular(SurfaceView sv, SurfaceMaterial m, float3 specColor, float specPower, float dayAlpha) {
    if (m.blinnPhong) {
        // レガシー(.x ChangeMaterial Specular/Power): RailSim II のBlinn-Phong加算モデルに合わせ、
        // 正規化係数を掛けない生pow(N.H,Power)をspecColorで加算する(F0/kdへは混ぜない＝diffuseを減衰させない)。
        return specColor * pow(sv.ndh, specPower) * sv.ndl * dayAlpha;
    }
    float3 fresnel = FresnelSchlick90(m.specF0, m.specF90, sv.vdh);
    return DirectSpecularGgx(fresnel, sv.ndl, sv.ndv, sv.ndh, m.directRoughness) * dayAlpha;
}

// 1つの光源(太陽・月)の直射光。lightColor は光源の色(太陽色×強さ)。fade は DirectDiffuse/DirectSpecular の dayAlpha。
// 太陽と月で同じ式を使う(拡散・鏡面とも、光源の方向と色だけが違う)。
struct DirectLighting {
    float3 diffuse;
    float3 specular;
};

DirectLighting EvaluateDirectLighting(float3 n, float3 wpos, float3 toLight, SurfaceMaterial m, float3 albedo,
                                      float3 specColor, float specPower, float fade, float3 lightColor) {
    SurfaceView sv = MakeSurfaceView(n, wpos, toLight);
    DirectLighting result;
    result.diffuse = DirectDiffuse(sv, m, albedo, fade) * lightColor;
    result.specular = DirectSpecular(sv, m, specColor, specPower, fade) * lightColor;
    return result;
}

// 環境光の強度。DXCの-Dでassets/config/render_tuning.jsonの値を受け取る。
static const float kAmbientIntensity = KAMBIENT_INTENSITY;
static const float kNightAmbientIntensity = KNIGHT_AMBIENT_INTENSITY;
static const float kNightAmbientFloor = KNIGHT_AMBIENT_FLOOR;

// 環境光(albedoを掛ける前の光。aoは掛け済み)。拡散の環境光に掛ける反射率(テクスチャ×材質Ambient)は呼び出し側。
// 太陽が沈むほど強まる固定の月明かり床を加算する(dayAlpha=1で0、dayAlpha=0で最大)。
// 色はわずかに青みがかった固定値。標準の空の暗順応(gAdaptation.y)が有効なときは床を使わない
// (夜の明るさは星明かりと月明かりが受け持つ)。
float3 AmbientLight(SurfaceView sv, SurfaceMaterial m, bool iblOn, bool pluginSky, bool useEnvMap, float dayAlpha) {
    float3 nightFloor = kNightAmbientIntensity * float3(0.05, 0.06, 0.09) * (1.0 - dayAlpha) * m.ao;
    if (gAdaptation.y != 0u) nightFloor = float3(0.0, 0.0, 0.0);
    // 拡散IBLの係数。EnvMap指定の材質(RailSim II -k-build のPBRシェーダを通る材質)は、-k-build と同じ
    // lerp(1 - 粗さ込みF, 0.01, metallic)。それ以外は直射光の拡散(DirectDiffuse)と同じ (1 - F) * (1 - metallic)
    // (鏡面に回ったぶんを拡散から引き、エネルギーを直射光と揃える。F は視線と法線の角度のSchlick)。
    float3 iblKd = useEnvMap ? lerp(1.0 - FresnelForIbl(m, m.f0, 1.0, sv.ndv), float3(0.01, 0.01, 0.01), m.metallic)
                             : (1.0 - FresnelSchlick(m.f0, sv.ndv)) * (1.0 - m.metallic);
    if (pluginSky) {
        // プラグインの空: 夜の暗さは Lighting.Set の表が持つので、dayAlpha・夜の床は使わない。
        // EnvMap指定の材質(と統一モードの全材質)は焼き済み拡散キューブ、それ以外は Lighting.Set の Ambient。
        float3 skyAmbient = (useEnvMap || IsPbrUnified(iblOn)) ? SampleIblDiffuse(sv.n) * iblKd : gPluginAmbient.rgb;
        return skyAmbient * m.ao;
    }
    if (iblOn) {
        // 昼側の環境光を拡散IBLへ置き換える。空が物理的に暗くなるのでdayAlphaは掛けない。
        float3 iblAmbient = gIblParams.y * SampleIblDiffuse(sv.n) * iblKd * m.ao;
        // 標準の空(暗順応が有効)は、月のない真夜中が真っ黒にならないよう環境光を下限へ切り上げる(足さないので月夜・昼は変わらない)。
        if (gAdaptation.y != 0u) iblAmbient = max(iblAmbient, kNightAmbientFloor * float3(0.8, 1.0, 1.4) * m.ao);
        return iblAmbient + nightFloor;
    }
    // マテリアルambient項もdayAlphaで減衰させる(直射光と同様、時刻に関係なく一定だと夜でも明るいままになる)。
    return kAmbientIntensity * m.ao * dayAlpha + nightFloor;
}

// 鏡面IBL(albedoは掛けない)。IBLが無効、またはBlinn-Phong分岐の材質は0(Blinn-Phong分岐の見た目は変えない)。
float3 IblSpecularLight(SurfaceView sv, SurfaceMaterial m, bool iblOn, bool pluginSky, bool useEnvMap) {
    if (!iblOn || m.blinnPhong) return float3(0.0, 0.0, 0.0);
    // RailSim II -k-build のPBRはEnvMap指定の材質だけが使うので、プラグインの空の鏡面IBLもその材質に限る。
    // 統一モードは全材質が使う(Specular色が黒の材質は specF0=0・specF90=0 で結果が0になる)。
    if (pluginSky && !useEnvMap && !IsPbrUnified(iblOn)) return float3(0.0, 0.0, 0.0);
    // BRDF LUT の A・B はフレネルの角度依存を積分済みなので、標準の空では LUT の F0 に本来の垂直反射率 m.f0 を渡す
    // (split-sum の標準形)。角度依存の FresnelForIbl を渡すとグレージング角でフレネルが二重にかかる。
    // プラグインの空は -k-build の再現のため FresnelForIbl を渡す。
    float3 fresnelIbl = pluginSky ? FresnelForIbl(m, m.specF0, m.specF90, sv.ndv) : m.specF0;
    return SampleIblSpecular(reflect(-sv.v, sv.n), sv.ndv, m.roughness, fresnelIbl, m.specF90) * m.specularAo;
}

// ライティング本体（不透明パス・半透明パス共通）。
// albedo/emissive とテクスチャ有無フラグ・法線/ORM のサンプル済み値を受け取る。テクスチャの Sample は
// 呼び出し側で済ませておく(ResourceDescriptorHeap[] 経由)。
// albedo: 直射光の拡散の反射率(テクスチャ × 材質Diffuse色)。
// emissive: 発光(テクスチャ色 × 材質Emissive色。呼び出し側で掛けて渡す)。
// ambientAlbedo: 環境光の反射率(テクスチャ × 材質Ambient色。XLoaderではAmbient=Diffuse)。
//   albedoと別に渡すのは、材質の色が環境光に2回掛かる(色の2乗になる)のを避けるため。
// specColor/specPower: ChangeMaterial の Specular(RGB)/Power(艶の鋭さ)。省略時(0,0,0,0)は ORM の roughness/metallic のみで決める。
// noReceiveShadow: 真なら太陽の影を受けない(材質ごとの指定。インスタンスごとにも変わる)。
SurfaceShading ShadeSurface(float3 wpos, float3 nrm, float2 uv,
                            float3 albedo, float3 ambientAlbedo, float3 emissive,
                            float3 sampledNormal, bool useNormal,
                            float3 sampledOrm, bool useOrm,
                            float3 specColor, float specPower, bool useEnvMap, bool noReceiveShadow,
                            float screenAo = 1.0) {
    // IBL(拡散・鏡面・太陽色・EnvMapの既定ORM)の有効フラグ。偽のときはIBLを使わない式。
    bool iblOn = gIblFlags.x != 0u;
    // プラグインの空(Envの焼き済みキューブ+Lighting.Set)。IBL有効のときだけ真になる(C++側でそうなるようにしている)。
    bool pluginSky = iblOn && IsPluginSky();
    bool standardSky = iblOn && !pluginSky;

    float3 n = normalize(nrm);
    if (useNormal) n = ApplyNormalMap(wpos, uv, n, sampledNormal);
    SurfaceMaterial m = ResolveSurfaceMaterial(albedo, sampledOrm, useOrm, specColor, specPower, useEnvMap, iblOn);
    m.directRoughness = SpecularAaRoughness(m.roughness, n); // 法線マップ適用後の法線の、画面上の変化量で広げる
    SurfaceView sv = MakeSurfaceView(n, wpos, normalize(-gLightDir.xyz));
    // 画面のAO(アンビエントオクルージョン)。環境光(拡散)と鏡面IBLだけに効き、直射光は暗くしない。
    // 材質のAO(ORMのR)とは小さいほうを使い、焼き込み済みの隙間の暗がりと二重に暗くならないようにする。
    // 拡散は多重反射の補正、鏡面は鏡面の遮蔽で和らげる。AOが無い(1)ときと、材質のAOのほうが暗いときは何も変えない。
    // 環境光は m.ao を掛けて求めるので、ここで求める倍率は「画面のAOに置き換えたときの比」。
    float3 ambientAoScale = float3(1.0, 1.0, 1.0);
    if (screenAo < m.ao) {
        ambientAoScale = MultiBounceAo(screenAo, ambientAlbedo) / m.ao;
    }
    if (screenAo < 1.0) {
        m.specularAo = min(m.specularAo, SpecularOcclusion(sv.ndv, screenAo, m.roughness));
    }
    float dayAlpha = saturate(gLightDir.w);
    // 直射光を時刻で弱める係数。標準の空は太陽色(透過率×地球の影)が沈む太陽を物理どおりに暗くするので掛けない。
    // IBL無効(太陽色=白)・プラグインの空(Lighting.Setの色)は太陽色が沈んでも暗くならないので dayAlpha を使う。
    float directFade = standardSky ? 1.0 : dayAlpha;

    // 直射光の色(IBL有効時は大気の透過率から求めた太陽色。dayAlphaの乗算は各項で済み)。
    float3 sunColor = float3(1.0, 1.0, 1.0);
    float adaptationGain = AdaptationGainFromTexture();
    if (iblOn) sunColor = pluginSky ? gPluginDirectional.rgb : MeshSunColor(sv.l) * adaptationGain;

    DirectLighting sunLighting = EvaluateDirectLighting(n, wpos, sv.l, m, albedo, specColor, specPower, directFade, sunColor);
    // 影は太陽の直射光(拡散と鏡面)にだけ掛ける。環境光・IBL・発光・月の直射光には掛けない。
    // 法線のずらしには、法線マップを掛ける前の頂点法線を使う。
    // 影を受けない材質(noReceiveShadow。NoReceiveShadow など)は係数1。
    float sunShadow = noReceiveShadow ? 1.0 : SunShadowScale(wpos, normalize(nrm), sv.l);
    float3 diffuse = sunLighting.diffuse * sunShadow;
    float3 specular = sunLighting.specular * sunShadow;
    // 月の直射光(標準の空のとき、gMoonLight.w > 0 の間だけ)。色は月の方向の透過率×地球の影 × 月の放射照度の比。
    // 沈んだ月は SunVisibilityAboveHorizon が0にするので、dayAlpha は掛けない(太陽と同じ)。
    if (standardSky && gMoonLight.w > 0.0) {
        float3 toMoon = normalize(gMoonLight.xyz);
        DirectLighting moonLighting = EvaluateDirectLighting(n, wpos, toMoon, m, albedo, specColor, specPower, 1.0,
                                                             MeshSunColor(toMoon) * (gMoonLight.w * adaptationGain));
        diffuse += moonLighting.diffuse;
        specular += moonLighting.specular;
    }
    float3 ambientLight = AmbientLight(sv, m, iblOn, pluginSky, useEnvMap, dayAlpha) * ambientAoScale;
    float3 iblSpecular = IblSpecularLight(sv, m, iblOn, pluginSky, useEnvMap);
    // 標準の空のIBL(拡散・鏡面)は、キューブを作ってからの明るさの変化を補正する(夜の床は暗順応の有効時は0なので影響しない)。
    if (standardSky) {
        const float iblCorrection = IblExposureCorrection();
        ambientLight *= iblCorrection;
        iblSpecular *= iblCorrection;
    }
    // 標準の空の鏡面は多重散乱の補正を掛ける。プラグインの空は掛けない(-k-build の再現)。
    if (standardSky && !m.blinnPhong) {
        float3 compensation = StandardSkyEnergyCompensation(m, sv.ndv);
        specular *= compensation;
        iblSpecular *= compensation;
    }
    // 法線が面とずれている画素(植物の板など)は鏡面を止める。RS_IBL=off は IBL 導入前と同じ絵のままにする。
    if (iblOn) {
        float mismatchScale = NormalMismatchSpecularScale(wpos, normalize(nrm), sv.v);
        specular *= mismatchScale;
        iblSpecular *= lerp(kNormalMismatchIblSpecularFloor, 1.0, mismatchScale);
    }

    SurfaceShading shading;
    shading.diffuseSide = float3(0.0, 0.0, 0.0);
    shading.specularSide = float3(0.0, 0.0, 0.0);
    // 開発用の確認(RS_DEBUG_IBL): 戻り値を該当の項だけにする。
    if (gIblFlags.y == 1u) { shading.diffuseSide = ambientLight; return shading; }
    if (gIblFlags.y == 2u) { shading.diffuseSide = iblSpecular; return shading; }
    if (gIblFlags.y == 3u) { shading.diffuseSide = sunColor; return shading; }
    // RailSim II の固定機能パイプラインはテクスチャステージがMODULATE(テクスチャ×頂点色)で、
    // 頂点色にはEmissiveも合成済みのため、発光もテクスチャの明暗パターンに変調される。
    // 発光は拡散色を掛けず、呼び出し側でテクスチャ色だけを掛けた値(emissive)を加える。
    // 拡散色が黒で発光だけで光る材質(LED表示など)が黒くならないようにするため。
    // 発光には暗順応の倍率(プリ露出)を掛けない(発光は物理の値でなく表示の明るさ)。
    shading.diffuseSide = ambientAlbedo * ambientLight + emissive + diffuse;
    shading.specularSide = specular + iblSpecular;
    return shading;
}
)HLSL";

} // namespace rs::render

//
// EnvironmentState.h - 環境光(IBL)・太陽色の受け渡し用の素の構造体
//
// FrameRenderer が毎フレーム1か所で組み立て、メッシュ描画の定数(MeshConstants)へ詰める。
// DX12型を持たない。SRV番号は共有SRVヒープの絶対番号で、メッシュのシェーダが
// ResourceDescriptorHeap[] で直接引く。
//
// 環境変数(いずれも BuildEnvironmentState が起動後に1回だけ読む開発用。設定画面・JSONには出さない):
//   RS_IBL=off          IBLの拡散・鏡面・太陽色・既定の金属ORMをすべて無効にし、固定の環境光に戻す。
//   RS_DEBUG_IBL=...    ambient / specular / suncolor。ShadeSurface の戻り値を該当の項だけにする
//                       (調整と目視確認用)。RS_IBL=off でも ambient は使える。
//   RS_DEBUG_ADAPTATION=1
//                       暗順応の倍率と照度を数フレーム後に読み戻してログへ出す(ExposureTarget。標準の空の暗順応の実測用)。
//   RS_DEBUG_APLUT=1    大気遠近(AP)のLUTの中身を数フレーム後に読み戻してログへ出す(AerialPerspectiveDebug。
//                       手前/奥 × 水平/上向きの rgb と a、水平・最大距離の a のCPU基準実装との比較)。
//   RS_STANDARD_SKY_PHOTO=off
//                       標準の空の環境ソース(IBLの元)へ、Envの写真の鏡面キューブを地平線より上の空の層として
//                       入れるのをやめる(Envがキューブ一式を持つときだけ層が入る。既定は入れる。
//                       明るさは標準の空へ正規化する)。
//
#pragma once

#include <cstdint>

namespace rs::render {

struct RenderTuning;

// ShadeSurface の戻り値の差し替え。値は MeshShader.h の gIblFlags.y と同じ。
enum class DebugIblMode : std::uint32_t {
    Off = 0,       // 通常
    Ambient = 1,   // albedo を掛けない環境光の項だけ(IBLが無効なら固定の環境光)
    Specular = 2,  // 鏡面IBLの項だけ
    SunColor = 3,  // 直射光に掛ける太陽色だけ
};

// IBLが引くSRVの絶対番号(持ち主: IblGenerator / AtmosphereLuts)。
struct EnvironmentSrvIndices {
    std::uint32_t diffuse = 0;
    std::uint32_t specular = 0;
    std::uint32_t brdfLut = 0;
    std::uint32_t transmittance = 0;
    std::uint32_t exposure = 0;  // 暗順応の結果(1x1。x=倍率)。標準の空(IBL)の srv だけが使う(IblGenerator::ExposureSrvIndex)
};

// プラグインの空(Envの焼き済みキューブ+Lighting.Set)の入力。app層が現在のEnvから作る。
// 色はEnvの値そのまま(sRGB、0〜1)。リニアへの変換は MakeEnvironmentState が行う。
struct PluginSkyInput {
    bool available = false;      // Envがキューブ一式を持ち、GPUへ上げ終えている(EnvCubeSet::Ready)
    bool hasLighting = false;    // Lighting.Set があった。偽なら直射=白・環境光=固定値にする
    float directional[3] = {1.0f, 1.0f, 1.0f};
    float ambient[3] = {0.5f, 0.5f, 0.5f};
    // 焼き済み拡散/鏡面キューブに掛けるRGB(sRGBエンコード。max(Directional, Ambient)、Lightingなしは1)。
    float meshLightScale[3] = {1.0f, 1.0f, 1.0f};
    EnvironmentSrvIndices srv;   // diffuse / specular / brdfLut だけ使う(焼き済みの3点)
    std::uint32_t specularMipCount = 1;
    // 鏡面キューブの写真の統計(EnvPhotoStats。標準の空が写真の空の層を使うときの正規化・方位合わせの入力)。
    bool photoStatsOk = false;
    bool photoSunFound = false;
    float photoSkyMean[3] = {0.0f, 0.0f, 0.0f};      // リニア。上半球のコサイン重み付き平均(太陽の周りを除く)
    float photoSunDirection[3] = {0.0f, 1.0f, 0.0f};  // 写真の座標の太陽の方向(単位ベクトル)
};

// Y軸回りの回転(水平面の (x,z) だけを回す)。x' = cos*x + sin*z、z' = -sin*x + cos*z。
// 環境ソースの焼き込みシェーダ(IblShader.h の RotateYaw)と同じ式。
struct YawRotation {
    float cos = 1.0f;
    float sin = 0.0f;
};

inline void RotateYaw(const YawRotation& rotation, float x, float z, float& outX, float& outZ) {
    outX = rotation.cos * x + rotation.sin * z;
    outZ = -rotation.sin * x + rotation.cos * z;
}

// 実際の太陽の方向(sunToSun)の水平成分を、写真の太陽の方向(photoSun)の水平成分へ重ねる回転。
// どちらかの水平成分が短すぎて方位が決まらないときは回転なし。
YawRotation MakeYawAlignment(const float sunToSun[3], const float photoSun[3]);

struct EnvironmentState {
    bool iblEnabled = false;
    DebugIblMode debugMode = DebugIblMode::Off;
    EnvironmentSrvIndices srv;
    std::uint32_t specularMipCount = 1;
    float iblDiffuseIntensity = 1.0f;
    float iblSpecularIntensity = 1.0f;
    float pluginSkyDiffuseIntensity = 1.0f;   // pluginSky のときだけシェーダが使う
    float pluginSkySpecularIntensity = 1.0f;
    float iblSpecularMipScale = 1.0f;                  // 標準の空の鏡面IBLのミップの倍率(RenderTuning::iblSpecularMipScale)
    float envMapMetallic = 1.0f;
    float envMapRoughness = 0.1f;

    // プラグインの空。pluginSky が真のとき、シェーダは生成IBL・大気の太陽色の代わりに下の値を使う。
    // 真になる条件は MakeEnvironmentState の1か所(プラグインの空の要求 かつ available かつ iblEnabled)。
    bool pluginSky = false;
    EnvironmentSrvIndices pluginSrv;
    std::uint32_t pluginSpecularMipCount = 1;
    float pluginDirectional[3] = {1.0f, 1.0f, 1.0f};  // リニア
    float pluginAmbient[3] = {0.0f, 0.0f, 0.0f};      // リニア。材質ambientに掛ける
    // リニア。メッシュの焼き済み拡散/鏡面キューブに掛ける。空パスの背景には使わない(背景は大気の計算)。
    float pluginMeshLightScale[3] = {1.0f, 1.0f, 1.0f};
    // 遠景(Landscape.x)のドームに掛ける。ディフューズを使わずアンビエントだけで照らすので、直射を含めない。
    // 色の数値(0〜1)をそのまま掛ける(リニアへ変換しない)。RailSim II の昼の見た目に合わせた値。Lighting.Set が無いときは1(無補正)。
    float pluginLandscapeLight[3] = {1.0f, 1.0f, 1.0f};

    // 標準の空の環境ソースへ、Envの写真の鏡面キューブを地平線より上の空の層として入れるか。真になる条件は
    // MakeEnvironmentState の1か所(IBL有効 かつ 標準の空 かつ キューブ一式が使える かつ 写真の統計が使える かつ
    // RS_STANDARD_SKY_PHOTO が off でない)。メッシュのシェーダは見ない(層は環境ソースの焼き込みで入る)。
    bool photoSky = false;
    // 写真を空だけでなく全方向(地平線より下も)に使い、遠景と大気の地面を重ねる代わりにする(既定)。
    // RS_STANDARD_SKY_PHOTO=sky のときは偽(地平線より上の空の層だけ)。photoSky が真のときだけ真になる。
    bool photoFullSphere = false;
    float photoSkyMean[3] = {0.0f, 0.0f, 0.0f};      // リニア。焼き込みが大気の空の明るさへ正規化する分母
    bool photoSunFound = false;
    float photoSunDirection[3] = {0.0f, 1.0f, 0.0f};  // 写真の座標。回転後の方向と比べて太陽マスクに使う
    YawRotation photoYaw;                              // 実際の太陽の方位を写真の太陽の方位へ重ねる回転

    // 月の光(直射光・月明かりの空・IBL)。標準の空(IBL有効、プラグインの空でない)で、太陽高度が
    // kMoonActiveMaxSunElevationDeg 以下のときだけ moonRatio が0より大きい(判定は MakeEnvironmentState の1か所)。
    // moonRatio = 満月の放射照度の比 × 月の明るさの比。メッシュ・空パス・IBLが同じ値を見る。
    float moonDirection[3] = {0.0f, 1.0f, 0.0f};  // 観測者から見た月の方向(単位ベクトル、Y-up)
    float moonRatio = 0.0f;                       // 太陽の放射照度に対する月の放射照度の比(0=月なし)

    // 空パスの月の円盤。moonRatio と違い、昼も描く(昼の月)。真になる条件は MakeEnvironmentState の1か所
    // (プラグインの空でない かつ 月の明るさが0より大きい)。満ち欠けの明暗は円盤の各点の陰影で出すので、明るさの比は使わない。
    bool moonDiskVisible = false;
    float moonDiskDirection[3] = {0.0f, 1.0f, 0.0f};  // 観測者から見た月の方向(単位ベクトル、Y-up)

    // 空の背景の放射輝度に掛ける倍率。空パスと大気遠近(AP)の散乱光が共用する(決めるのは MakeEnvironmentState の1か所)。
    // 標準の空は skyRadianceScale、プラグインの空は skyRadianceScale × pluginSkyAtmosphereScale。暗順応の倍率は含まない。
    float skyRadianceScale = 1.0f;

    // 標準の空の暗順応(プリ露出)。真のとき、(1) 光を計算する側へ srv.exposure の倍率を先に掛ける(環境ソースの焼き込み・
    // メッシュの直射光・空の背景。発光とトーンマップには掛けない)、(2) メッシュの夜の床と空の夜の青を使わない
    // (星明かりと月明かりが受け持つ)。
    // 真になる条件は MakeEnvironmentState の1か所(IBL有効 かつ プラグインの空でない)。偽(RS_IBL=off・プラグインの空)のときは暗順応を使わない。
    bool adaptation = false;
};

// skyMode の値からプラグインの空の要求を決める(大文字小文字を区別しない)。
// "plugin" は真、"standard" は偽、nullptr・それ以外は fallback。
bool PluginSkyRequestedFromEnvValue(const char* value, bool fallback);

// RS_IBL の値から有効かどうか。"off"(大文字小文字を区別しない)のときだけ無効。nullptr・それ以外は有効。
bool IblEnabledFromEnvValue(const char* value);

// atmosphericPerspective の値から大気遠近を使うかを解釈する("off"のときだけ偽)。
bool AerialPerspectiveEnabledFromEnvValue(const char* value);


// heightFog の値から高さフォグを使うかを解釈する("off"のときだけ偽)。
bool HeightFogEnabledFromEnvValue(const char* value);


// RS_DEBUG_IBL の値から表示モード(大文字小文字を区別しない)。未設定・未知の値は Off。
DebugIblMode DebugIblModeFromEnvValue(const char* value);

// RS_STANDARD_SKY_PHOTO の値から、標準の空の環境ソースへ写真を入れるか。
// "off"(大文字小文字を区別しない)のときだけ偽。nullptr・それ以外は真(既定は入れる)。
bool StandardSkyPhotoFromEnvValue(const char* value);

// RS_STANDARD_SKY_PHOTO の値から、写真を全方向に使うか。"sky"(大文字小文字を区別しない)のときだけ偽
// (地平線より上の空の層だけ)。nullptr・それ以外は真(既定)。
bool StandardSkyPhotoFullSphereFromEnvValue(const char* value);

// 実際の太陽の方向(観測者から見た向き。Y-up)。写真の太陽との方位合わせに使う。
struct SunToSunInput {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
};

// 月の方向(観測者から見た向き。Y-up)と、満月を1とする明るさの比(MoonState::brightness)。app層が作る。
struct MoonInput {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
    float brightness = 0.0f;  // 0なら月なし
};

// 環境変数の値を引数で受ける組み立て(テスト用に分けてある)。iblReady が偽なら環境変数に関係なく無効。
// プラグインの空は pluginSkyRequested が真で pluginSky.available が真で、IBLが有効なときだけ有効
// (RS_IBL=off ならプラグインの空も切れる)。
EnvironmentState MakeEnvironmentState(const RenderTuning& tuning, const EnvironmentSrvIndices& srv,
                                      bool iblReady, const char* iblEnvValue, const char* debugEnvValue,
                                      const PluginSkyInput& pluginSky = PluginSkyInput{},
                                      bool pluginSkyRequested = false,
                                      const char* standardSkyPhotoEnvValue = nullptr,
                                      const SunToSunInput& sunToSun = SunToSunInput{},
                                      const MoonInput& moon = MoonInput{});

// 開発用環境変数(RS_IBL・RS_DEBUG_IBL・RS_STANDARD_SKY_PHOTO。初回の呼び出しで1回だけ読む)を使って組み立てる。
// pluginSkyPreferred は --setting skyMode の指定(既定は真。偽は開発用に標準の空へ固定する)。
// pluginSky を渡さない呼び出し(サムネイル)は、常に標準の空になる。
EnvironmentState BuildEnvironmentState(const RenderTuning& tuning, const EnvironmentSrvIndices& srv,
                                       bool iblReady, const PluginSkyInput& pluginSky = PluginSkyInput{},
                                       bool pluginSkyPreferred = false,
                                       const SunToSunInput& sunToSun = SunToSunInput{},
                                       const MoonInput& moon = MoonInput{});

} // namespace rs::render

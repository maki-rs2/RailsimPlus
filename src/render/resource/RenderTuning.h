// RenderTuning.h - レンダリング資源・シェーダの調整パラメータ(assets/config/render_tuning.json)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string_view>

namespace rs::render {

// APのLUT(RenderTuning::aerialPerspectiveSliceCount・TileSize)の上限。横に並べた幅(スライス数×タイル)が
// テクスチャの最大幅(16384)に収まるようにする。
constexpr int kAerialMaxSliceCount = 64;
constexpr int kAerialMaxTileSize = 128;
static_assert(kAerialMaxSliceCount * kAerialMaxTileSize <= 16384, "2Dテクスチャの最大幅に収まること");

struct RenderTuning {
    std::uint64_t modelCacheBudgetBytes = 256ull * 1024 * 1024;
    std::uint64_t textureCacheBudgetBytes = 512ull * 1024 * 1024;
    float cutoutOccluderAlphaThreshold = 0.95f;
    // 穴あきテクスチャのアルファを「穴(下限未満)・半透明(下限以上、上限未満)・不透明(上限以上)」に分ける境目。
    // 半透明のtexelが非ゼロtexelに占める割合が semiAlphaMinFraction 以上の穴あき材質は、同じsubmeshを2回に分けて描く。
    // 不透明(上限以上)は穴あきと同じ描画で深度を書き、半透明は全不透明の後に深度を書かずにブレンドする。
    float semiAlphaLow = 0.02f;
    float semiAlphaHigh = 0.95f;
    float semiAlphaMinFraction = 0.005f;
    // 環境光の強度。RailSim II の D3DRS_AMBIENT=0x808080(ガンマ空間の0.5)をリニア換算した値。
    float ambientIntensity = 0.255f;
    // 夜間の環境光下限(月明かり相当)の強度。太陽が沈み光源強度(dayAlpha)が
    // 0へ近づくほど加算量が増える固定の環境光床。
    // IBL無効(RS_IBL=off)のときだけ使う。標準の空(IBL有効)は星明かりと月明かりが受け持ち、プラグインの空はLighting.Setの表が持つ。
    float nightAmbientIntensity = 0.10f;
    // 標準の空(暗順応が有効)の環境光の下限。拡散の環境光を max(環境光, この値 × 青みの色) へ切り上げる(albedoを掛ける前、暗順応を掛けた後の値)。
    // 月のない真夜中だけが下限に届き、月夜・薄明・昼は環境光のほうが明るいので変わらない。0なら切り上げなし。
    float nightAmbientFloor = 0.03f;
    // 直射光の鏡面ハイライトのスペキュラAA(画面上の法線の変化量に応じて roughness を広げる)の強さ。
    // 0で無効、1で標準(Kaplanyan 2016 / Tokuyoshi の方式。分散の係数0.25・上限0.18)。大きいほど、遠くの曲面や
    // 法線マップのちらつきが強くぼける。近くの平らな面のハイライトはほとんど変わらない。
    float specularAaStrength = 0.0f;
    // 暗順応(標準の空だけ。render/sky/IblGenerator が測る順応の照度 E に応じた倍率を、光を計算する側へ先に掛ける
    // プリ露出: 環境ソースの焼き込み・メッシュの直射光・空の背景。発光とトーンマップには掛けない)。
    // E は、空の上半球のコサイン重み付き平均の放射輝度の輝度(月明かりの空・星明かりを含む)で、太陽の放射照度πに対する比の単位
    // (直射光は入れない)。
    // 倍率 = max(夜の曲線, 昼の曲線) を clamp(1, nightAdaptationMaxGain) したもの。各曲線は、E が開始の照度以上なら1、
    // そうでなければ (開始 / E)^指数。夜の曲線は nightAdaptation*、昼の曲線は dayAdaptation*(render/sky/AtmosphereMath の ExposureAdaptationGain)。
    // nightAdaptationStart: 夜の曲線が働き始める照度。夜の曲線の値が使われるのは、昼の曲線より大きくなる暗さだけ。
    float nightAdaptationStart = 0.02f;
    // 順応の強さ(0〜1)。1なら夜も順応の開始時と同じ明るさに見え、0なら順応しない。
    float nightAdaptationExponent = 0.8f;
    // 倍率の上限。
    float nightAdaptationMaxGain = 1000000.0f;
    // 深夜の上乗せ。上の倍率に、太陽高度-10度から-13度にかけて1からこの値まで増える係数を掛ける
    // (render/sky/AtmosphereMath の NightAdaptationBoost)。月の有無によらず、深夜の画面全体を一律に明るくする。1なら上乗せなし。
    float nightAdaptationBoost = 1.5f;
    // 昼の曲線。太陽が低い昼から薄明にかけて、画面が暗く沈まないよう倍率を上げる。
    // dayAdaptationStart: この照度より暗いと上げ始める(太陽がほぼ真上の空の照度に置く)。
    float dayAdaptationStart = 0.17f;
    // 昼の曲線の強さ(0〜1)。大きいほど夕方も昼に近い明るさになり、0なら昼の曲線は働かない。
    float dayAdaptationExponent = 0.7f;
    // トーンマップ(HDR→バックバッファ)前に掛ける露出倍率。1.0で無補正。
    // 本編とサムネイルの撮影で共通に使う(render/postprocess/TonemapPass)。
    float tonemapExposure = 1.0f;
    // 空(大気LUT)の放射輝度に掛ける倍率。1.0で無補正(render/sky/SkyPass)。
    float skyRadianceScale = 1.3f;
    // 太陽の円盤の明るさ(透過率LUTの色に掛ける。物理値はfp16を超えるため大きく抑えた値)。
    float sunDiskIntensity = 30.0f;
    // 標準の空で、太陽が物を照らす直射光(メッシュ・粒子・遠景・霧)の強さ。skyRadianceScale に対する比で持つ
    // (直射光に掛かる倍率は SunLightRadianceScale)。1.0なら空と同じ倍率で、日なたと日陰(空の光)の明るさの比が物理どおりになる。
    // 太陽の円盤とレンズフレアには掛けない(円盤は sunDiskIntensity)。0以上、100以下。範囲外の値は ParseJson が既定値へ戻す。
    float sunLightScale = 1.0f;
    // 太陽の円盤の角直径[度]。実際の太陽は約0.53度。
    float sunDiskAngularDiameterDeg = 0.53f;
    // 大気のエアロゾルの光学的厚さ(AOD、波長によらず1つ。render/sky/AtmosphereParams.h の MieScatteringPerKm)。
    // 晴天の実測は世界平均で約0.1〜0.15、都市部で0.2〜0.3。大きいほど空が明るく白っぽくなり、直射光は弱まる。
    // 標準の空と、プラグインの空の背景の両方に反映される(プラグインの空のメッシュの環境光は焼き済みキューブなので反映されない)。
    float atmosphereAerosolOpticalDepth = 0.05f;
    // 空(Sky-View LUT)と大気遠近のLUTを積分する観測者の高さを、カメラの高さに追従させるか。偽なら基準の高さ
    // (render/sky/AtmosphereParams.h の kObserverHeightKm)に固定する。ワールドの高さ0のカメラは、真でも偽でも同じ見た目。
    bool atmosphereFollowCameraHeight = true;
    // 大気遠近(AP)のLUT(render/sky/AerialPerspectiveLut)。視錐台を、距離方向の aerialPerspectiveSliceCount 枚の
    // スライス(各 aerialPerspectiveTileSize 四方の画素)に区切り、横に並べた1枚の2Dテクスチャに持つ。
    // 最大距離[km]より遠い画素は、最後のスライスの値で止める。距離は手前を細かく割る(スライス位置の2乗)。
    // 資源の大きさは起動時に決まるので、起動後には変えない。範囲外の値は ParseJson が既定値へ戻す。
    int aerialPerspectiveSliceCount = 32;
    int aerialPerspectiveTileSize = 32;
    float aerialPerspectiveMaxDistanceKm = 64.0f;
    // 高さフォグ(render/pipeline/MeshFogShader)。密度は ρ(h) = ρ0 × exp(-(h - h0) / H)。h はワールドの高さ(Y、メートル)。
    // 既定値は物理量の見積もりではなく、見た目で調整する初期値。範囲外の値は ParseJson が既定値へ戻す。
    //   heightFogDensity       基準の高さ h0 での密度 ρ0 [1/m]。0なら霧は出ない(0以上、0.05以下)。
    //   heightFogHeightScale   密度が 1/e になる高さ H [m](1以上、5000以下)。
    //   heightFogBaseHeight    基準の高さ h0 [m](-1000以上、10000以下)。
    //   heightFogAlbedo        霧の反射率。霧に当たる光に掛ける(0以上、1以下)。
    float heightFogDensity = 0.0005f;
    float heightFogHeightScale = 150.0f;
    float heightFogBaseHeight = 0.0f;
    float heightFogAlbedo = 1.0f;
    // IBL(render/sky/IblGenerator)を作り直す、太陽高度の変化の閾値[度]。
    float iblRegenerateThresholdDeg = 1.0f;
    // IBLの拡散・鏡面の寄与に掛ける倍率。1.0で無補正(render/pipeline/MeshShader)。
    float iblDiffuseIntensity = 1.0f;
    float iblSpecularIntensity = 1.0f;
    // 標準の空の鏡面IBLで、粗さからミップを選ぶときの倍率。1.0=焼き方どおり(ミップ = roughness × (ミップ数-1))。
    // 0.5で RailSim II -k-build のPBRシェーダと同じ鋭さ(roughness × 4、9段のキューブ)になり、映り込みが本来より約2倍くっきりする
    // (物理的には違う。法線マップや金属度・粗さの違いを見えやすくする見た目の調整)。
    float iblSpecularMipScale = 1.0f;
    // 標準の空でEnvの写真を全方向の環境に使うとき(EnvironmentState::photoFullSphere)だけ、鏡面IBLに掛ける倍率。1.0で無補正。
    // 写真は大気の空の明るさへ正規化しているので、-k-build の見た目(写真のHDR値を直射光=1とそのまま並べる単位)より
    // 映り込みが約5〜7倍暗い。JSONの値は、2つの車両で側面の映り込みがプラグインの空に近くなるよう見た目で決めた
    // (物理的には違う)。
    float photoSpecularGain = 1.0f;
    // プラグインの空(Envの焼き済みキューブ)の拡散・鏡面IBLに掛ける倍率。1.0で無補正。
    // 上の2つは標準の空だけに反映されるので、プラグインの空は別に持つ。
    float pluginSkyDiffuseIntensity = 1.0f;
    float pluginSkySpecularIntensity = 1.0f;
    // プラグインの空の背景(標準の空と同じ大気の計算で描く)と大気遠近(AP)の散乱光に、skyRadianceScale へ重ねて掛ける倍率。
    // プラグインの空はトーンマップを通さず(saturate のみ)暗順応もないので、空が飽和しにくい明るさにそろえる値。
    // 既定の1.0は、太陽が低いときの太陽の周りの白飛びを、標準の空と同じくらいに抑える値(大きくすると昼の空は明るくなるが、白飛びの範囲が広がる)。
    // 0より大きく100以下。範囲外の値は ParseJson が既定値へ戻す。
    float pluginSkyAtmosphereScale = 1.0f;
    // プラグインの空で、Envの Sun.x・Moon.x を時刻に合わせた位置へ別メッシュとして描くか(render/sky/SkyBodyPass)。
    // RailSim II と同じ描き方だが、-k-build が同じ描き方をしているかは未確認なので、偽にすればすぐ切れる。標準の空には関係ない。
    bool pluginSkyBodies = true;
    // レンズフレア・白飛びはリニアHDRのシーンターゲットへ、トーンマップの前に足す。定義の色はsRGBの値なので
    // リニアへ変換してから掛ける。暗順応の倍率は掛けない。値は見た目で決めた(物理量ではない)。
    // flareHdrIntensity: フレアの要素のRGBに掛ける倍率。1.0だとトーンマップ前の白(1.0)がそのまま足される。
    //   0.8は、RailSim II より明るく見えすぎないよう1.0から少し落とした値。
    // whiteoutHdrLevel: 白飛びの色(リニア)に掛ける倍率。アルファで画面へ寄せる先の明るさで、
    //   0.8は、白がトーンマップ(ACES)の肩で白に潰れず、RailSim II の白飛びと同程度に見える水準。
    float flareHdrIntensity = 0.8f;
    float whiteoutHdrLevel = 0.8f;
    // ブルーム(render/postprocess/BloomPass)。露出を掛けるトーンマップの前に、HDRのシーンへ混ぜる。
    // bloomThreshold: 滲ませる明るさのしきい値(露出後のHDR。画素の最大チャンネルがこれを超えた分だけが滲む)。
    //   発光はテクスチャ色×材質の Emissive 色(どちらも1以下)で、露出を掛けても1.3前後までなので、1.0では灯具が滲まない。
    //   窓ガラス(Blended)・半透明は合成済みの値を読むので、ガラスの奥の発光はガラスの分だけ暗くなる。0.5は、その発光も滲み、
    //   昼の車両の車体では滲まない値。
    // bloomStrength: 滲んだ光をシーンへ加算する倍率(0でブルームなし。0〜10)。見た目で決める。
    // bloomScatter: 小さい段の結果を1段上へ混ぜる割合(0〜1)。大きいほど広く滲む。
    float bloomThreshold = 0.5f;
    float bloomStrength = 0.5f;
    float bloomScatter = 0.7f;
    // TAA(render/postprocess/TaaPass)。見た目で決める。
    // taaHistoryWeight: 静止している画素で履歴に掛ける重み(0〜0.99)。大きいほどなめらかで、残像・ぼけが出やすい。
    // taaCutDistanceM: 前フレームから視点がこの距離[m]以上動いたら、カメラの瞬間移動とみなして履歴を捨てる。
    // taaSharpness: 解決後に足すシャープの強さ(0で無し)。
    float taaHistoryWeight = 0.95f;
    float taaCutDistanceM = 50.0f;
    float taaSharpness = 0.0f;
    // ボリュメトリックフォグ(render/effects/VolumetricFogPass)。見た目と負荷で決める。
    // volumetricFogDistance: 格子の奥行きの範囲[m](10以上、2000以下)。
    // volumetricFogGridPixelSize: 格子の1セルの画面上の大きさ[画素](4以上、64以下)。
    // volumetricFogSliceCount: 格子の奥行きの分割数(8以上、128以下)。
    // volumetricFogAnisotropy: 位相関数の偏り(-0.9以上、0.9以下)。正で太陽の向きが明るくなる。
    // volumetricFogSunIntensity: 霧に当たる太陽の光の倍率(0以上、100以下)。
    // volumetricFogHistoryWeight: 前フレームの結果を混ぜる割合(0以上、0.98以下)。大きいほどノイズが減り、影の動きへの追従が遅れる。
    float volumetricFogDistance = 200.0f;
    int volumetricFogGridPixelSize = 16;
    int volumetricFogSliceCount = 64;
    float volumetricFogAnisotropy = 0.6f;
    float volumetricFogSunIntensity = 2.0f;
    float volumetricFogHistoryWeight = 0.9f;

    // モーションブラー(render/postprocess/MotionBlurPass)。見た目で決める。
    // motionBlurShutter: 1フレームの動きのうち、ぼかしの長さにする割合(0〜2。0でぼかしなし)。0.5がシャッター角 180° 相当、0.65 が約 234° 相当。
    // motionBlurMaxRadius: ぼかしの半径(中心から前後へ広げる片側の長さ)の上限(画面の高さに対する割合。0.001〜0.1)。速いカメラの動きで画面全体が流れすぎないようにする。
    //   速度のタイル1枚の大きさ(64 画素)でも頭打ちになる。
    // photoMotionBlurMaxRadius: ファインダー中(シャッター速度に連動するとき)の motionBlurMaxRadius。遅いシャッターで大きく流すため広げる。
    // motionBlurSamples: 速度の向きに拾う点の数(4〜32)。多いほどなめらかで、重い。
    float motionBlurShutter = 0.65f;
    float motionBlurMaxRadius = 0.02f;
    float photoMotionBlurMaxRadius = 0.05f;
    int motionBlurSamples = 12;
    // フォトモードの被写界深度(render/postprocess/DepthOfFieldPass)。見た目で決める。
    // dofMaxRadius: ぼかしの半径の上限(出力の幅に対する割合。0.001〜0.1)。解像度が変わっても最大のぼけ方が同じになる。
    //   半解像度のタイル2枚ぶん(フル解像度で96画素)でも頭打ちになる。
    // dofSamples: 円盤の中で拾う点の数(8〜64)。多いほどなめらかで、重い。
    float dofMaxRadius = 0.04f;
    int dofSamples = 32;
    // フォトモードの ISO オート(app/camera/PhotoAutoIso)。見た目で決める。
    // photoAutoIsoTarget: 画面の目標の明るさ(測光した輝度 x トーンマップの露出 x 撮影カメラの倍率が、この値になる ISO を選ぶ。0.001〜10)。
    //   大きいほど明るく写る。基準のカメラ(ISO100・F5.6・1/125秒)で昼の標準的なシーンが ISO100 付近になるよう決める。
    // photoAutoIsoSeconds: ISO が目標へ追いつく時定数[秒](0〜10。0 で即座に合わせる)。
    float photoAutoIsoTarget = 0.2f;
    float photoAutoIsoSeconds = 0.5f;
    // アンビエントオクルージョン(render/postprocess/AmbientOcclusionPass の GTAO)。見た目で決める。
    // aoRadius: 遮蔽を探す半径[m]。大きいほど広い範囲の暗がりを拾い、画面上の半径が大きいぶん重くなる。
    // aoFalloffRange: 半径のうち、外側の何割で遮蔽の重みを0へ落とすか(0〜1)。遠い物の影響が急に切れないようにする。
    // aoFinalPower: 求めた明るさ(0〜1)に掛ける冪。大きいほど暗がりが濃くなる。
    float aoRadius = 0.5f;
    float aoFalloffRange = 0.6f;
    float aoFinalPower = 1.8f;
    // 標準の空の組み込み遠景(render/sky/LandscapeCube)の調整。プラグインの空には反映されない。
    // 明るさの倍率(1.0で無補正)・彩度(0=白黒、1=元の色)・RGBの色の倍率・方位のずらし[度。正で、方位が
    // 増える向きへ遠景が回って見える]。
    float landscapeBrightness = 1.0f;
    float landscapeSaturation = 1.0f;
    float landscapeTintR = 1.0f;
    float landscapeTintG = 1.0f;
    float landscapeTintB = 1.0f;
    float landscapeYawDeg = 0.0f;
    // 遠景の明暗の幅(1.0で無補正)。アルベドの輝度を、中間の灰色0.18を中心に対数の上で何倍に広げるか
    // (render/sky/LandscapeShader.h の ApplyLandscapeContrast)。反射率は1を上限にする。
    float landscapeContrast = 1.0f;
    // EnvMap指定でORMを持たない材質の既定ORM(AOは1)。金属感のある反射にする。
    float envMapMetallic = 1.0f;
    float envMapRoughness = 0.1f;
    // ORMもEnvMap指定もなく、Specular/Powerも持たない材質の粗さ(AOは1、金属度は0)。
    // 全材質PBR統一モード(RS_PBR_UNIFIED)でも、この条件の材質に使う。
    float defaultRoughness = 0.8f;
    // 同じ条件の材質の金属度。全材質PBR統一モード(RS_PBR_UNIFIED)のときだけ使い、`RS_PBR_UNIFIED=0`では0のまま。
    float defaultMetallic = 0.1f;
    // 統一モードで、ChangeMaterialのSpecular色(Blinn-Phongのハイライトの強さ)を鏡面の垂直反射率F0へ換算する倍率。
    // 白(1.0)をそのままF0にすると全反射になり、鏡面IBLで白飛びする。
    float specularF0Scale = 0.3f;
    // 統一モードで、Specular/PowerのPowerから換算した粗さに掛ける倍率。小さいほど鏡面がくっきりする。
    float powerRoughnessScale = 1.0f;
    // 統一モードで、EnvMap指定・ORMなし・Specular/Powerありの材質に使う金属度と粗さ(RailSim II の球マップの映り込みに近づける)。
    float envSpecMetallic = 0.8f;
    float envSpecRoughness = 0.25f;
    // 静的シーナリー用GPUインスタンスバッチ(InstanceBatchManager)の常駐上限
    // (バッチ個数、basePath単位)。Scene切替で非表示になったバッチを即破棄せず
    // この件数までparkし、複数Sceneを往復してもGPU再アップロードが発生しないように
    // する。
    std::size_t staticInstanceBatchResidencyCap = 512;
    // 静的配置の_CAMDISTバンドを視点から再計算する頻度[Hz]。0以下なら毎フレーム更新する。
    float camDistBandUpdateHz = 10.0f;

    // シャドウ(render/shadow)。太陽の直射光だけに影を落とす。
    // 品質プリセット(なし/低)はユーザー設定(settings.dat の shadowQuality)で選ぶ。ここには持たない。
    // 影を出す視点からの最大距離[m]。これより遠くは影なし。末尾の shadowFadeFraction の割合でなだらかに消える。
    // どれを使うかはユーザー設定(settings.dat の shadowDistance、近い/標準/遠い。render/shadow/ShadowDistance.h)。
    float shadowDistanceNear = 50.0f;
    float shadowDistanceStandard = 100.0f;
    float shadowDistanceFar = 200.0f;
    float shadowFadeFraction = 0.1f;
    // カスケードの分割の混合係数(0=等間隔、1=対数)と、境界でブレンドする範囲の幅(受け持つ距離に対する割合)。
    float shadowSplitLambda = 0.75f;
    float shadowCascadeBlendFraction = 0.1f;
    float shadowFarCascadeSunAngleDeg = 0.25f;
    // 細い部品(レール・架線・ハンガー)を影に描くカスケードの数(近い方から数える)。遠い枚では細部が見えないので描かない。
    int shadowThinCascadeCount = 2;
    // 影の強さ(0〜1)。直射光(拡散と鏡面)に 1 - (1 - 可視) * 強さ を掛ける。環境光・IBL・発光には掛けない。
    // 1にすると影の中の直射光が完全に消え、0.65なら35%残る。
    float shadowStrength = 0.65f;
    // 法線方向へずらして影を引く量[テクセル]。斜めの面の自己影(シャドウアクネ)を抑える。大きいほど影が面から浮く。
    float shadowNormalOffsetTexels = 1.5f;
    // 影の縁をぼかす半径[m](ワールド長)。シェーダがカスケードのテクセルの世界幅で割ってテクセル数にする。
    // タップ数は品質プリセットが持つ。0なら1タップ(ぼかさない)。
    float shadowBlurRadiusM = 0.30f;
    // 細い部品の影を残す品質(高)のぼけ幅[m]。レール1本の幅より狭くする。
    float shadowBlurRadiusNarrowM = 0.10f;
    // 高の接触硬化(PCSS)。ぼけ半径 = 遮蔽物との距離 × 光源の大きさ を、細い方のぼけ半径と上限の間に収める。
    // 上限は遮蔽物を探す半径でもある。遮蔽物とみなす深度差の下限は、自己影を拾わないための値。
    float shadowPcssLightSize = 0.03f;
    float shadowPcssMaxRadiusM = 0.5f;
    float shadowPcssBlockerBiasM = 0.03f;
    // シャドウパスのラスタライザの深度バイアス(D32_FLOAT なので、DepthBias は深度の最下位ビット単位)と、
    // 面の傾きに比例するスロープスケール深度バイアス。
    int shadowDepthBias = 1000;
    float shadowSlopeScaledDepthBias = 1.5f;
    // シャドウパスのカリング: 0=なし、1=表面(Front)を捨てる、2=裏面(Back)を捨てる。
    // 表面を捨てると自己影は減るが、薄い板は影が抜ける。
    int shadowCullMode = 0;
    // 視錐台の外のライト側にある影を落とすものが届くよう、ライト側へ延ばす距離[m]。
    float shadowCasterExtensionM = 300.0f;
    // 影を出す太陽の高度[度]の下限と、そこから影が最大の強さになるまでの幅[度]。下限以下は影なし
    // (影が極端に長くなり、細い範囲に解像度を使い切るため)。夜(dayAlpha=0)の影も0。
    float shadowMinSunElevationDeg = 1.0f;
    float shadowSunElevationFadeDeg = 2.0f;

    // 太陽の角半径[rad](角直径の半分)。空の太陽の円盤と、メッシュの直射光の鏡面の広がりが共通に使う。
    float SunAngularRadiusRad() const {
        return 0.5f * sunDiskAngularDiameterDeg * std::numbers::pi_v<float> / 180.0f;
    }

    // プラグインの空の背景・AP の散乱光に掛ける放射輝度の倍率(skyRadianceScale × pluginSkyAtmosphereScale)。
    float PluginSkyRadianceScale() const { return skyRadianceScale * pluginSkyAtmosphereScale; }

    // 標準の空で、太陽が物を照らす直射光に掛ける倍率(skyRadianceScale × sunLightScale)。
    float SunLightRadianceScale() const { return skyRadianceScale * sunLightScale; }

    // JSON文字列を解析する。JSONとして読めない・トップがオブジェクトでない・キーの型が違うときは nullopt。
    // 欠けたキーと範囲外のキーは、そのキーだけ既定値になる。
    static std::optional<RenderTuning> TryParseJson(std::string_view json);
    // JSON文字列を解析する(欠けたキーは既定値、解析失敗時は全部既定値。テスト用に公開)。
    static RenderTuning ParseJson(std::string_view json);
    // 調整値の現在の版。Releaseでは埋め込みJSONの値(解析に失敗したときは構造体の既定値)のまま。
    // Debugでは PublishReloaded で公開された最新版を指すことがある。返した参照は、後で版が差し替わっても
    // 指す先が消えない(古い版は残る)。
    static const RenderTuning& Embedded();
    // 読み直した値を新しい版として公開する。メインスレッド専用(版の公開は1つのスレッドからだけ行う)。
    // 公開より前に取った Embedded() の参照は古い版のままなので、読む側は使う時点で Embedded() を取り直す。
    static void PublishReloaded(RenderTuning tuning);
};

} // namespace rs::render

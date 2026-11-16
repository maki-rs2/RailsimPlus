//
// PluginSession.h - 読込済みプラグイン1つの実行時状態をまとめて保持する
//
// LoadedPlugin(パース/メッシュ結果)に加え、表示中パーツ・カント上書き・可動部/
// マテリアルのランタイム・スイッチ値表を1つにまとめる。GPUアップロードやカメラ
// 操作・フォント再構築は含まず(呼び出し側=main.cppのレンダリング層が担当)、
// データ保持とロジック(ランタイム構築・スイッチ解決)のみを受け持つ。
//
#pragma once

#include <optional>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../plugins/load/runtime/AnimationRuntime.h"
#include "../../plugins/load/runtime/HeadlightWorldTracker.h"
#include "../../plugins/load/runtime/ParticleWorldTracker.h"
#include "../../plugins/load/runtime/MaterialRuntime.h"
#include "../../plugins/load/runtime/MoverRuntime.h"
#include "../../plugins/load/runtime/SoundEffectAttachment.h"
#include "../../plugins/load/runtime/SoundEffectWorldTracker.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/schema/SystemSwitchAxes.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SceneSharedSwitches.h"
#include "../../world/time/SolarPosition.h"
#include "PluginSessionRuntimeMetadata.h"
#include "PluginSwitchTable.h"

namespace rs::app {

// PluginSession::IsFullyStatic()の判定要素を種別ごとに名前付きで持つ結果構造体。
// 全カスタマイザ種別(ChangeModel/ChangeTexture/ChangeMaterial/SetAnimation/
// TextureTransform)を横断する高頻度スイッチ参照判定(referencesHighFrequencySwitch)
// を含め、判定根拠を1箇所へ集約する(判定基準自体はIsFullyStatic()と同一)。
struct StaticEligibility {
    // 時間で動く可動部(DynamicRotate/AnalogClock)を持つ。Move/Rotateだけなら静止するので
    // 焼き込める(PluginSession::hasTimeDrivenMovers、StaticMeshVariantBake.cpp)。
    bool hasTimeDrivenMovers = false;
    bool hasAnimations = false;
    bool hasSoundEffects = false;
    bool referencesHighFrequencySwitch = false; // Dynamicな軸(_HOUR等)の参照
    bool referencesCamDistBand = false;         // StaticByBandな軸(_CAMDIST)の参照
    bool referencesSharedStateAxis = false;     // StaticBySharedStateな軸(_NIGHT/_SEASON/_SHADOW/_ENVMAP)の参照

    // 可動部・サウンド・Dynamicな軸の参照がいずれも無い。アニメは定義にあっても、スイッチ値に
    // よっては動かないので、ここでは問わない(動くかどうかは BakeStaticMeshVariants が値ごとに見る)。
    bool IsBakeableExceptAnimation() const {
        return !hasTimeDrivenMovers && !hasSoundEffects && !referencesHighFrequencySwitch;
    }

    // 上に加えてアニメの定義も無い(_CAMDISTと共有状態の軸_NIGHT/_SEASON/_SHADOW/_ENVMAPの参照の
    // 有無は問わない。この種の軸は値ごとに焼き分けて静的にできる)。
    bool IsStaticExceptCamDistBand() const {
        return IsBakeableExceptAnimation() && !hasAnimations;
    }

    // _CAMDISTも共有状態の軸も参照しない = メッシュが1つに決まる静的プラグイン。
    bool IsFullyStatic() const {
        return IsStaticExceptCamDistBand() && !referencesCamDistBand && !referencesSharedStateAxis;
    }
};

// PluginSession::IsFullyStatic()が使う判定要素をdefから計算する(plugin.okは含まない、
// 呼び出し側の責務)。hasTimeDrivenMoversにはPluginSession::hasTimeDrivenMoversを渡す。
// カメラ距離(_CAMDIST)や時刻(_HOUR/_MINUTE/_SECOND)のような
// 高頻度・配置固有に変わる変数へ依存するスイッチ式/条件式を持つ場合は
// referencesHighFrequencySwitchをtrueにする(該当変数の値はインスタンス間で
// 共有できないため、共有マテリアル状態に固定するインスタンス化と両立しない)。
// stationRuntimeConstantがtrueなら、駅舎の実行時スイッチ(_APPROACH1/2・_STOPPING)を
// 高頻度スイッチに数えない(駅の接近・停車が無く常に0の配置、信号プラグインのモデル用)。
inline StaticEligibility ComputeStaticEligibility(const rs::legacy_pi::PluginDefinition& def,
                                                   bool hasTimeDrivenMovers,
                                                   bool stationRuntimeConstant = false) {
    StaticEligibility result;
    result.hasTimeDrivenMovers = hasTimeDrivenMovers;
    result.hasAnimations = !def.animations.empty();
    result.hasSoundEffects = !def.soundEffects.empty();
    result.referencesHighFrequencySwitch = rs::legacy_pi::DefinitionReferencesAnySwitch(
        def, stationRuntimeConstant ? rs::legacy_pi::SystemSwitchNamesDynamicExceptStationRuntime()
                                    : rs::legacy_pi::SystemSwitchNamesWithVerdict(
                                          rs::legacy_pi::SystemSwitchStaticVerdict::Dynamic));
    result.referencesCamDistBand =
        rs::legacy_pi::DefinitionReferencesAnySwitch(
            def, rs::legacy_pi::SystemSwitchNamesWithVerdict(
                     rs::legacy_pi::SystemSwitchStaticVerdict::StaticByBand));
    result.referencesSharedStateAxis =
        rs::legacy_pi::DefinitionReferencesAnySwitch(
            def, rs::legacy_pi::SystemSwitchNamesWithVerdict(
                     rs::legacy_pi::SystemSwitchStaticVerdict::StaticBySharedState));
    return result;
}

struct PluginSession {
private:
    PluginSwitchTable switches_;

public:
    PluginSession() = default;
    PluginSession(const PluginSession& other) = default;
    PluginSession& operator=(const PluginSession& other) = default;
    PluginSession(PluginSession&& other) noexcept = default;
    PluginSession& operator=(PluginSession&& other) noexcept = default;

    // ---- 読込結果と表示・調整状態(UIパネルが読み書きする) ----
    rs::legacy_plugin::LoadedPlugin plugin;
    // 情景配置のロード元フォルダ。SoundEffect の waveFile 絶対パス解決にのみ使う。
    std::string loadedFolderUtf8;
    float curveRadiusM = 0.0f;
    float cantOverrideDeg = -1.0f;
    // 複合線形(Line+Clothoid+Arc)でBuildRailMeshAlongSplineを試すプレビュー用
    // トグル(既定OFFならBuildRailMesh経路)。
    bool useSplinePreview = false;

    // ---- 可動部/マテリアルのランタイム(def.parts と1:1対応) ----
    std::vector<rs::legacy_plugin::PartMoverRuntime> moverRuntimes;
    std::vector<rs::app::SoundEffectAttachment> soundEffectAttachments;
    std::vector<rs::app::SoundEffectRuntimeState> soundEffectStates;
    std::vector<rs::app::HeadlightAttachment> headlightAttachments;
    std::vector<rs::app::HeadlightRuntimeState> headlightStates;
    std::vector<rs::app::ParticleAttachment> particleAttachments;
    std::vector<rs::app::ParticleRuntimeState> particleStates;
    std::vector<rs::legacy_plugin::PartMaterialRuntime> materialRuntimes;
    std::vector<rs::legacy_plugin::PartAnimationRuntime> animationRuntimes;
    size_t movablePartCount = 0;
    size_t movableGroupCount = 0;
    // DynamicRotate/AnalogClockの可動部が1件でもあるか(BuildRuntimes時に算出)。静的
    // インスタンス化の可否判定の唯一の入力(Move/Rotateのみなら焼き込める)。
    bool hasTimeDrivenMovers = false;
    // 時間駆動のSetAnimationを持つパーツが1つでもあるか(BuildRuntimes時に算出)。
    // レイアウト配置スロットの毎フレームアニメ更新の要否判定に使う。
    bool hasTimedAnimation = false;
    // SetAnimation/texture-transformer(ShiftTexture等)のいずれかを1件でも持つ
    // パーツが1つでもあるか(BuildRuntimes時に算出、PartAnimationRuntime::
    // HasEntries参照)。hasTimedAnimationと異なりtexture-transformer単体の
    // パーツもtrueになる。配置済みTrain車両は_FRONT等のシステムスイッチが
    // ApplySwitchOptionsAndBakeの読込時焼き込み後に変化しうるため、texture-
    // transformerのみのパーツでも毎フレーム再評価が必要
    // (If !"_FRONT"{ShiftTexture}Else{ShiftTexture} のような式で、Else側が
    // 個体反転車両でも反映されなくなるため)。
    bool hasAnimationRuntimeEntries = false;

    // DefineSwitch 由来(初期0)
    std::map<std::string, int32_t>& UserSwitches() { return switches_.userSwitches; }
    const std::map<std::string, int32_t>& UserSwitches() const { return switches_.userSwitches; }
    // "_名前" 参照から自動収集(初期0)
    std::map<std::string, int32_t>& SystemSwitches() { return switches_.systemSwitches; }
    const std::map<std::string, int32_t>& SystemSwitches() const { return switches_.systemSwitches; }
    std::map<std::string, int32_t>& RoutineSwitchOverrides() { return switches_.routineSwitchOverrides; }
    const std::map<std::string, int32_t>& RoutineSwitchOverrides() const {
        return switches_.routineSwitchOverrides;
    }

    // userSwitches/systemSwitches の内容が前回 RefreshSwitchGeneration() 時点
    // から変化したかどうかを表す世代カウンタ(未変化なら同値のまま)。
    // PartMaterialRuntime::Apply / PartAnimationRuntime::Update /
    // PartMoverRuntime::UpdateActivation へ渡し、「スイッチ値が変わっていない
    // フレームでは customizer 群の発動判定(IsTriggerActive)を丸ごとスキップし、
    // 前回の判定結果を使い回す」ためのキー。customizerが非常に多い
    // 発車標のようなプラグインでは、この世代キャッシュと
    // EvalSwitchExprのAST事前コンパイル化(SwitchExpr.cpp)の組で
    // フレーム時間を大幅に減らせる。
    uint64_t& SwitchGeneration() { return switches_.switchGeneration; }
    uint64_t SwitchGeneration() const { return switches_.switchGeneration; }

    // 全 PartAnimationRuntime が共有するアニメーション時計(実時間の絶対秒)。
    // main.cpp が毎フレーム1回 io.DeltaTime を加算し、各 Runtime::Update へ
    // そのまま渡す。各エントリは fmod(この時計, 周期) を再生位相とするため、
    // 同一周期のアニメ(発車標の行先/種別/両数など)は全パーツで切り替え
    // タイミングが揃い、フレームレートにも依存しない。
    double animationClockSeconds = 0.0;

    // userSwitches/systemSwitches を前回呼び出し時点のスナップショットと比較し、
    // 変化していれば switchGeneration をインクリメントする。毎フレーム1回、
    // 各 Runtime へ generation を渡す前に呼ぶこと(main.cpp)。比較コストは
    // スイッチ数(数十件程度)に比例するだけで、customizer件数には依存しない。
    void RefreshSwitchGeneration();

    // folderUtf8: parsedの元になったロード元フォルダ(UTF-8)。ParsedPluginCache経由の
    // キャッシュヒット時もSoundEffectのwaveFile絶対パス解決ができるよう、呼び出し側は
    // 読込元の実フォルダを渡すこと(実フォルダが無い場合のみ空文字列でよい)。
    void LoadFromParsed(const rs::legacy_plugin::LoadedPlugin& parsed, const std::string& folderUtf8 = std::string());

    // ユーザースイッチ→システムスイッチの順に現在値を解決する(未登録は0)。
    // PIの If/ApplySwitch 式評価に渡す SwitchResolver として使う。
    int32_t ResolveSwitch(const std::string& name) const;

    SwitchEvaluationInput BuildSwitchEvaluationInput(
        const std::vector<int32_t>* switchOptions = nullptr,
        std::optional<int32_t> camDist = std::nullopt,
        std::optional<rs::world::SceneSharedSwitchValues> sharedState = std::nullopt) const;

    // 焼き込みでも実行時でも、同じ入力の優先順位と距離の量子化を使う。
    void ApplySwitchEvaluationAndBake(const SwitchEvaluationInput& input);

    void ApplyRoutineSwitchOverrides(const std::map<std::string, int32_t>& overrides);

    // `_CAMDIST`の量子化バンド開始値(昇順・重複なし)。UpdateCamDistは生の距離を
    // 直近下のバンド開始値へ丸めて書くため、これが「_CAMDISTが実行時に取りうる値」の
    // 集合そのものになる(テクスチャ事前ロードの仮定値列挙が使う)。
    // ただし解析できない条件式を含むプラグインでは量子化を行わず生の距離が入るため、
    // その場合は完全な集合ではない(CamDistQuantizationSafe()を参照)。
    const std::vector<int32_t>& CamDistBandStarts() const;
    bool CamDistQuantizationSafe() const;
    float DayAlpha() const;

    // システムスイッチ "_CAMDIST"(視点からユニット基準座標までの距離。視野角
    // 45°基準でtan(x/2)に比例してスケールした値、メートル単位・四捨五入)を
    // 現在のカメラ位置から再計算し systemSwitches へ書き込む。unitOrigin省略時は
    // ワールド原点(0,0,0)を基準とする(ビューア用途)。配置済みTrain車両の
    // ように実際のワールド座標を持つ場合は unitOrigin にそれを渡すこと。
    // plugin側が "_CAMDIST" を実際に参照している場合のみ(CollectSwitchTablesで
    // 登録済みの場合のみ)更新する。呼び出し側(main.cpp)は毎フレーム、
    // スイッチ解決より前に呼ぶこと。
    // viewerFovYRadiansは実カメラの垂直視野角(FlyCamera::fieldOfView)。
    void UpdateCamDist(const float eye[3], const float* unitOrigin, float viewerFovYRadians);
    // プラグインが`_CAMDIST`を参照しているか。参照しないスロットでは、UpdateCamDistに
    // 渡す配置位置の計算ごと省くための問い合わせ。
    bool ReferencesCamDist() const;

    // 時刻系システムスイッチ("_YEAR"/"_MONTH"/"_DAY"/"_DAYOFWEEK"/"_HOUR"/
    // "_MINUTE"/"_SECOND"/"_NIGHT"/"_SEASON"/"_SHADOW"/"_ENVMAP")を GameClock/SolarPosition/緯度
    // から再計算し systemSwitches へ書き込む。_CAMDIST と同様、11キーそれぞれ
    // について「plugin側が実際に参照している場合のみ」(=systemSwitchesに
    // キーが存在する場合のみ)更新する。_NIGHT は sun.directionToSunY(=sin(太陽高度)、
    // RailSim IIのCEnvPlugin.cppのsunaltと同じくsin値であって弧度ではないことに注意)を
    // nightThresholdSinと比較して判定する。_SEASON は clock.Month() から算出し、
    // latitudeDeg が負(南半球)なら(season+2)%4で反転する。_SHADOW は shadowEnabled(影を実際に描く設定か)を
    // そのまま書く。_ENVMAP は envMapEnabled(実際にIBLを使っているか)をそのまま書く。この4つの値は
    // rs::world::ComputeSceneSharedSwitchValues(静的インスタンス経路と共用)で求める。呼び出し側(main.cpp)は
    // UpdateCamDist と同じ箇所・タイミング(ChangeModel/ApplySwitchのトリガー判定
    // より前)で毎フレーム呼ぶこと。
    void UpdateTimeSwitches(const rs::world::GameClock& clock, const rs::world::SolarState& sun,
                            double latitudeDeg, double nightThresholdSin, bool shadowEnabled,
                            bool envMapEnabled, bool writeCalendar = true);
    // writeCalendar が偽なら、日付・時刻・_SEASON を書かない(プラグインビューアーが設定の値のまま保つため)。
    // 昼夜係数・_NIGHT・_SHADOW・_ENVMAP は書く。

    // システムスイッチ "_VELOCITY"/"_ACCEL"/"_FRONT"/"_CONNECT1"/"_CONNECT2"/"_SERIAL"
    // (配置済みTrain車両専用)を書き込む。
    // velocityKmh: 符号つき現在速度([km/h]、内部で絶対値化する)。
    // accelMhPerS: TrainRuntime::GroupCurrentAccelMhPerSの生値(丸めなし、内部で四捨五入)。
    // front: この車両の局所+Zが現在の走行方向と不一致ならtrue(RailSim IIの
    // mrev=(編成側m_Reverse==車両個体側m_Reverse)否定と同義)。呼び出し側は
    // 個体反転フラグ(reversed)と現在の走行方向(後退中=負の速度)のXORを渡すこと。個体反転の有無だけで固定される値ではなく、
    // 折返し運転等で走行方向が変わるたびに変化しうる。
    // connectFront/connectRear: 車両ローカルの前方/後方に隣接車両があるか
    // (reversedのときは呼び出し側で"_CONNECT1"/"_CONNECT2"に対応するよう
    // 入れ替えて渡すこと。このメソッド自身は入れ替えを行わない)。
    // serial: 所属編成の識別番号("_SERIAL"にそのまま書き込む)。
    // door1/door2: 編成全体で共有するドア開閉状態(
    // 車両ごとのreversedによる入れ替えは行わない)。
    // 他のUpdate*系と同様、systemSwitchesに該当キーが既に存在する場合のみ書く。
    void UpdateTrainSwitches(double velocityKmh, double accelMhPerS, bool front,
                             bool connectFront, bool connectRear, int serial);

    // 駅舎(Station)プラグイン向けの接近判定スイッチ("_APPROACH1"/"_APPROACH2"/"_STOPPING")を
    // 書き込む。値は駅舎内のPlatform出現順に対応する
    // 32bitビットマスク。他のUpdate*系と同様、systemSwitchesに該当キーが既に存在する
    // 場合のみ書く。
    // 戻り値: 実際に値が変化したか。呼び出し側はtrueのときスロットを再計算対象へ
    // 戻す必要がある(SceneSceneryRuntime::Tickの不変スキップ対策)。
    bool UpdateStationSwitches(int32_t approach1, int32_t approach2, int32_t stopping);

    // ChangeModel を持つ全パーツについて、現在のスイッチ値(resolve)で
    // rs::legacy_pi::ResolveChangeModel を再評価し、ロード時/前回評価時から
    // 表示すべき候補が変わっていれば rs::legacy_plugin::ApplyModelVariant で
    // plugin.models[]/assemblyMesh を差し替え、対応する materialRuntimes も
    // 新しいベースマテリアルで作り直す。GPUアップロードは行わない(呼び出し側
    // main.cpp が戻り値=true のときだけ表示中メッシュを再アップロードする)。
    bool UpdateDynamicModels(const rs::legacy_pi::SwitchResolver& resolve);

    const std::vector<uint32_t>& DynamicMaterialIndices() const {
        return dynamicMaterialIndices_;
    }

    // assemblyMesh に焼き込み済みの内容を表す署名(スイッチの今の値ではない)。
    std::string ResolvedGeometrySignature() const {
        return ResolveGeometrySignature(plugin);
    }

    // .rs2レイアウトのSwitchOption(配置保存時点のスイッチ確定値、DefineSwitch宣言順に
    // 位置対応)を userSwitches へ書き込み、ChangeModel(UpdateDynamicModels)と
    // ChangeTexture/ChangeMaterial/ChangeAlpha(PartMaterialRuntime::Apply)を1回だけ
    // 解決してassemblyMesh.materialsへ焼き込む。GPU非依存で、静的なUV変換も含む。
    // 時間駆動のアニメは、描画時に共通の適用関数で重ねる。
    // switchOptionsがplugin.def.switchesより短い/長い場合は前方のみ対応させ、
    // 余りは無視する(SwitchNum不一致時の防御)。
    // camDistBandに値があり、プラグインが_CAMDISTを参照するときは、スイッチの解決より前に
    // 実行時と同じ規則でバンド開始値へ量子化する。解析できない条件では生の値を使う。
    // nulloptならセッションの現在値から入力を組み立てる。
    // sharedStateに値があるときは、プラグインが参照している共有状態の軸(_NIGHT/_SEASON/_SHADOW/_ENVMAP)だけへ
    // その値を書く(参照していない軸には書かない)。nulloptなら触れない。
    void ApplySwitchOptionsAndBake(
        const std::vector<int32_t>& switchOptions,
        std::optional<int32_t> camDistBand = std::nullopt,
        std::optional<rs::world::SceneSharedSwitchValues> sharedState = std::nullopt);

    // 実行時に見た目が一切変化しない(=GPUインスタンス化しても安全な)プラグインか
    // 判定する(大規模レイアウトのScenery大量配置で、SceneRegistry常駐スロットが
    // 枠を超えないようにするための判定)。InstanceBatchManagerは1バッチ=1マテリアル状態を
    // 全インスタンスで共有するため、時間経過で自動的に見た目が変わる(時間駆動の可動部=
    // DynamicRotate/AnalogClock、DefineAnimation)プラグインを複数配置すると全インスタンスが同じ状態に
    // 固定されてしまい壊れる。
    //
    // switches(DefineSwitch)の有無は判定に含めない: 常駐エンティティのスイッチは
    // ApplySwitchOptionsAndBake()でロード直後に1回だけ.rs2のSwitchOptionへ確定させ、
    // 以後はUIからも自動更新スイッチ("_CAMDIST"等、main.cppのグローバルsessionのみ
    // 対象)からも変化しない。つまりswitchesを持つプラグインでもTryGetStaticInstanceMesh()
    // がキャプチャするassemblyMesh(ApplySwitchOptionsAndBake後の状態)は以後ずっと
    // 不変であり、instance化しても安全(ただし同一folderで異なるSwitchOptionを持つ
    // 配置は別バケットにする必要がある。呼び出し側=SceneRenderer/EditorViewportが
    // folder+スイッチ確定値でキャッシュ/バッチを分ける)。
    //
    // SoundEffectを持つプラグインもインスタンス化対象から除外する: 静的インスタンス
    // バッチはSceneSlotPool::Slot(個別PluginSessionのランタイム更新)を経由しないため、
    // ここへ合流すると音源のワールド位置追跡・スイッチ発動判定(PlacedSoundRequestCollector/
    // SceneSceneryRuntime::Tick)が一切行われず、無音のまま気付きにくく壊れる
    //
    // LoadFolder後に呼ぶこと(plugin.ok前提、失敗プラグインはfalse)。
    // 加えて、カメラ距離(_CAMDIST)や時刻(_HOUR/_MINUTE/_SECOND)のような高頻度・
    // 配置固有に変わる変数へ依存するスイッチ式/条件式を持つ場合も除外する
    // (該当変数の値はインスタンス間で共有できないため、共有マテリアル状態に
    // 固定するインスタンス化と両立しない)。
    bool IsFullyStatic() const {
        return plugin.ok && ComputeStaticEligibility(plugin.def, hasTimeDrivenMovers).IsFullyStatic();
    }

    bool HasDynamicStretchZy() const;
    // 書き換え範囲を持つコピーはジオメトリの更新まで保持する。assemblyMeshには範囲を設定しない。
    const rs::render::Mesh& SceneUploadMesh();

private:
    std::optional<rs::render::Mesh> sceneUploadMesh_;
    // moverRuntimes/materialRuntimes/animationRuntimes を plugin から構築し、可動部の数を数える。
    void BuildRuntimes();
    // ApplySwitchOptionsAndBake()が上書き前に戻す「素の状態」のassemblyMesh.materials
    // スナップショット。上書き(ChangeTexture/ChangeMaterial/ChangeAlpha)はr.hasTextureOverride
    // 等の条件が立った要素にしか書かないため、セッションを使い回して2回目以降
    // ApplySwitchOptionsAndBakeを呼ぶと前回のスイッチ組み合わせの上書きが残留しうる。
    // これを防ぐために毎回このスナップショットへ戻してから上書きループへ入る。
    std::vector<rs::render::Material> pristineAssemblyMaterials_;
    bool hasPristineAssemblyMaterials_ = false;
    std::vector<uint32_t> dynamicMaterialIndices_;
    uint64_t lastDynamicModelSwitchGeneration_ = UINT64_MAX;
};

} // namespace rs::app

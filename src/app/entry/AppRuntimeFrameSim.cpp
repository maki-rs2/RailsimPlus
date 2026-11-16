#include "../../render/RenderPrecision.h"
//
// AppRuntimeFrameSim.cpp - RunFrameLoop()のシミュレーション関連ステージ
// (固定tick/GameClock/太陽位置/システムスイッチ/Sync/sceneTickループ)
//
#include "AppRuntimeFrameInternal.h"
#include "../../platform/EnvironmentVariable.h"
#include "ViewerMode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>

#include <spdlog/spdlog.h>

#include "../plugin_session/PluginRootResolver.h"
#include "../plugin_session/PluginSkyLighting.h"
#include "../plugin_session/RailSoundLookup.h"
#include "../../platform/text/Encoding.h"
#include "../../world/time/SceneSharedSwitches.h"
#include "../scene/PlacedSoundRequestCollector.h"

namespace rs::app {

namespace {

// ゲーム内時計と情景のTickの固定刻み(60Hz)。
constexpr double kFixedTimeStepSeconds = 1.0 / 60.0;

// 開発用: 環境変数 RS_SUN_ELEVATION_DEG(度)で太陽の高度を固定する(設定画面・JSONには出さない)。
// 起動時に1回だけ読む。値の解釈は rs::world::ParseSunElevationOverrideDeg。
std::optional<double> ReadSunElevationOverrideDeg() {
    return rs::world::ParseSunElevationOverrideDeg(rs::platform::ReadEnvironmentVariable("RS_SUN_ELEVATION_DEG"));
}

// 開発用: 環境変数 RS_MOON_ELEVATION_DEG(度)で月の高度、RS_MOON_PHASE(位相角[度]、0=満月)で
// 位相角と明るさを固定する(設定画面・JSONには出さない)。起動時に1回だけ読む。
// 値の解釈は rs::world::ParseMoonElevationOverrideDeg / ParseMoonPhaseOverrideDeg。
std::optional<double> ReadMoonElevationOverrideDeg() {
    return rs::world::ParseMoonElevationOverrideDeg(rs::platform::ReadEnvironmentVariable("RS_MOON_ELEVATION_DEG"));
}

std::optional<double> ReadMoonPhaseOverrideDeg() {
    return rs::world::ParseMoonPhaseOverrideDeg(rs::platform::ReadEnvironmentVariable("RS_MOON_PHASE"));
}

// 月の位置と満ち欠けを、太陽と同じ時刻・緯度・地軸傾斜・強制モードで求める。Envの Moon(公転周期・初期位相)は
// 現在のEnvから、なければ既定。開発用の上書きは太陽の上書き(RS_SUN_ELEVATION_DEG)と同じ形で最後に当てる。
rs::world::MoonState ComputeFrameMoon(const AppRuntimeGlobals& globals, const ActiveEnv& activeEnv,
                                      double latitudeDeg, double axialInclinationDeg,
                                      const rs::world::SolarOverrides& solarOverrides) {
    rs::world::MoonParams params;
    if (const rs::legacy_pi::EnvDefinition* env = activeEnv.Definition()) {
        params.revolutionPeriodDays = env->moonRevolutionPeriodDays;
        params.initialPhaseDays = env->moonInitialPhaseDays;
    }
    rs::world::MoonState moon = rs::world::ComputeMoonPosition(globals.gameClock, latitudeDeg,
                                                               axialInclinationDeg, params, solarOverrides);
    static const std::optional<double> kElevationOverrideDeg = ReadMoonElevationOverrideDeg();
    static const std::optional<double> kPhaseOverrideDeg = ReadMoonPhaseOverrideDeg();
    if (kElevationOverrideDeg.has_value()) rs::world::ApplyMoonElevationOverride(moon, *kElevationOverrideDeg);
    if (kPhaseOverrideDeg.has_value()) rs::world::ApplyMoonPhaseOverride(moon, *kPhaseOverrideDeg);
    return moon;
}

// 編成を描画へ提出し、このフレームで継目を跨いだ車軸の車輪音の要求を返す。
// シミュレーション速度が1倍より大きいときは、距離だけ追って要求は出さない。
std::vector<rs::sim::WheelSoundEvent> SubmitTrainsAndCollectWheelSounds(FrameLoopState& loopState,
                                                                        const FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    WheelSoundFrame wheelFrame;
    wheelFrame.findRail = [&state](const std::string& folderUtf8) -> const TrackPluginEntry* {
        return state.pluginSelector.FindTrackPluginEntry(folderUtf8);
    };
    wheelFrame.frameMs = static_cast<double>(loopState.io.DeltaTime) * 1000.0;
    wheelFrame.audible = globals.gameClock.TimeScale() <= 1.0;
    std::vector<rs::sim::WheelSoundEvent> wheelEvents;
    state.sceneRenderer.SubmitTrainRenderSnapshot(globals.app, globals.editor.graph, scratch.eye,
                                                  globals.gameClock, scratch.sunState,
                                                  scratch.solarLatitudeDeg,
                                                  state.activePreset->nightThresholdSin,
                                                  scratch.fieldOfView, wheelFrame,
                                                  wheelEvents);
    return wheelEvents;
}

} // namespace

void AdvanceSimulation(FrameLoopState& loopState, FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    ImGuiIO& io = loopState.io;

    constexpr int kMaxTicksPerFrame = 10; // フレームスパイク時の無限追いつき防止

    // GameClockを固定tick(60Hz)で進める。RailSim II(CSaveFile::Simulate)の
    // 実装(m_Frame += scale; を speed 回繰り返す)では、
    // TimeScaleは「1tickあたりの進む量」であり「tick回数」ではない。そのため蓄積は実時間のみ(TimeScale
    // を掛けない)で行い、tick回数は実時間側で固定(~60Hz)に保ち、1tickあたりに
    // 進めるゲーム内時間を kFixedTimeStepSeconds*TimeScale にする。こうしないと
    // 高TimeScale時にtick回数上限(kMaxTicksPerFrame)で時計が頭打ちになり、
    // 指定された速度で進まなくなる。
    // sceneRenderer.Tick(列車物理+可動部アニメ)も同じgameDtで、このフレームの
    // Sync()が終わった後で同じtick回数だけ実行する(下記
    // gameTicksThisFrame参照。ドア開閉等の可動部アニメも列車走行と同じく
    // 早送りされる、RailSim II準拠)。
    // GameClock::Tick自身はdtへtimeScale_を掛けないため、ここで1回だけ掛ける。
    scratch.gameDtPerTick = kFixedTimeStepSeconds * globals.gameClock.TimeScale();
    scratch.gameTicksThisFrame = 0;
    // レイアウト読込・Scene切替のフレーム分割ジョブが進行中の間は、GameClock進行(信号・編成Tick・可動部アニメの駆動源)を
    // 一時停止する。fixedTickAccumulator自体への加算も止めることで、ジョブ完了後に
    // 溜め込んだ経過時間が一気に消化される急激な早送りを防ぐ。
    if (!state.sceneRenderer.SyncJobActive()) {
        loopState.fixedTickAccumulator += io.DeltaTime;
        while (loopState.fixedTickAccumulator >= kFixedTimeStepSeconds &&
              scratch.gameTicksThisFrame < kMaxTicksPerFrame) {
            globals.gameClock.Tick(scratch.gameDtPerTick);
            loopState.fixedTickAccumulator -= kFixedTimeStepSeconds;
            ++scratch.gameTicksThisFrame;
        }
        // 追いつき上限に達したフレームでは、未消化の整数tick負債を破棄する。
        // 1 tick未満の位相だけを残すことで、長いフレーム後も毎フレーム上限回数を
        // 実行し続ける自己増幅を断ち、操作応答を優先する。GameClock/走行系は実際に
        // 実行したtickと同じgameDtPerTickだけ進むため、TimeScaleの適用位置は変わらない。
        if (scratch.gameTicksThisFrame == kMaxTicksPerFrame) {
            loopState.fixedTickAccumulator = std::fmod(loopState.fixedTickAccumulator, kFixedTimeStepSeconds);
        }
    }
    // アクティブSceneのEnvを追い、太陽の通り道の緯度を1回だけ決める(Envが緯度を持たなければ
    // 環境プリセットの値)。太陽位置・昼夜判定・季節スイッチ(配置物・編成の分も含む)は
    // すべてこの値を使う。1か所でも別の値を渡すと、空と昼夜・季節がずれる。
    // RS_ENV_OVERRIDE(開発用)の名前解決に使うルート候補。上書きが指定されている初回の1回だけ呼ばれる。
    state.activeEnv.Update(state.layoutSession, [&state]() {
        return BuildPluginRootCandidates(
            std::string(), state.userSettings.pluginRoots,
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring()));
    });
    // Envが変わったら、そのEnvの焼き済みキューブ一式を非同期に読み、GPUへ上げる(見た目にはまだ使わない)。
    state.envCubeController.Update(state.activeEnv, globals.app.EnvCubes(), globals.app.SkyBodies(),
                                   [&globals]() { globals.app.WaitForGpu(); });
    state.sunFlareController.Update(state.activeEnv, state.flareTextureSlots, globals.app.LensFlare(),
                                    globals.app.SunFlare());
    scratch.solarLatitudeDeg = state.activeEnv.SolarLatitudeDeg(state.activePreset->latitudeDeg);
    // 太陽と月は同じ上書きを使う(ビューアーモードはライトの設定、通常はレイアウトの値)。
    const rs::world::SolarOverrides& solarOverrides = EffectiveSolarOverrides(globals, state);
    scratch.sunState = rs::world::ComputeSolarPosition(
        globals.gameClock, scratch.solarLatitudeDeg, state.activePreset->axialInclinationDeg,
        solarOverrides);
    // 開発用の高度の上書き。ここで sunState を書き換えるので、下の昼夜係数・SetSunDirection・
    // 昼夜スイッチ(UpdateTimeSwitches)のすべてに同じ値が流れる。
    static const std::optional<double> kSunElevationOverrideDeg = ReadSunElevationOverrideDeg();
    if (kSunElevationOverrideDeg.has_value())
        rs::world::ApplySunElevationOverride(scratch.sunState, *kSunElevationOverrideDeg);
    // レンダラへ渡す lightDir(光が進む向き)は directionToSun(観測者から見た
    // 太陽の向き)の符号反転(double→floatの変換もこの1箇所に集約する)。
    // dayAlphaはPluginSwitchTable::UpdateTimeSwitchesの_NIGHT判定と同じ
    // 昼夜係数(ComputeDayAlpha)を光源強度・空色描画へ渡す。
    const double dayAlpha = rs::world::ComputeDayAlpha(scratch.sunState.directionToSunY,
                                                        state.activePreset->nightThresholdSin);
    globals.app.SetSunDirection(rs::render::ToGpuFloat(rs::core::DVec3{-scratch.sunState.directionToSunX,
                                                                    -scratch.sunState.directionToSunY,
                                                                    -scratch.sunState.directionToSunZ}),
                                rs::render::ToGpuFloat(dayAlpha));
    // 月(標準の空の月明かり)。double→floatの変換は太陽と同じくこの1箇所。月の光を使うかは描画側が決める。
    const rs::world::MoonState moonState = ComputeFrameMoon(globals, state.activeEnv, scratch.solarLatitudeDeg,
                                                            state.activePreset->axialInclinationDeg, solarOverrides);
    globals.app.SetMoon(rs::render::ToGpuFloat(rs::core::DVec3{moonState.directionToMoonX, moonState.directionToMoonY,
                                                          moonState.directionToMoonZ}),
                        rs::render::ToGpuFloat(moonState.brightness));
    // プラグインの空の入力。Lighting.Setの補間は太陽高度のsin(directionToSunY)で引く(RailSim IIと同じ)。
    globals.app.SetPluginSkyInput(
        MakePluginSkyInput(state.activeEnv, state.envCubeController, scratch.sunState.directionToSunY));

    // 実効カメラの視点(eye/forward)を決める。
    // この後のsceneRenderer.Tick(配置物・Train車両の_CAMDIST算出)・
    // オーディオリスナー向き(SyncAndTickScene)に使い回す。カブ視点(運転台視点)が有効な間は俯瞰カメラ(FlyCamera)の
    // 更新自体が止まる(UpdateCameraAndEditorが意図的にスキップする)ため、俯瞰
    // カメラの値をそのまま使うと切替直前の位置に固定されてしまう。「現在の実効
    // カメラ視点」をCabinViewCamera側で1箇所に解決し、それが取れればそちらを
    // 優先する。
    float cabinUpUnused[3];
    if (!globals.cabinCamera.TryResolveEyeForward(scratch.eye, scratch.forward, cabinUpUnused)) {
        globals.camera.fly.EyePos(scratch.eye);
        globals.camera.fly.Forward(scratch.forward);
    }
    // ビューアーモードは背景色もライトに合わせる(変わったフレームだけ描画先を作り直す)。
    if (state.viewerMode) globals.app.SetViewerBackground(state.viewer.lightOn);

    const double nowSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool dirty = HasUnsavedLayout(globals, state);
    // 読込ジョブ進行中は自動復元スナップショットの書き込みも止める(registryが
    // 読込途中の半端な状態のまま保存されるのを防ぐ)。
    if (!state.sceneRenderer.SyncJobActive() &&
        ShouldWriteAutoRecovery(state.autoRecoverySchedule, dirty,
                                globals.editor.placementDragUi.active, nowSeconds)) {
        state.autoRecoverySchedule.lastAttemptSeconds = nowSeconds;
        const LayoutActionResult result = state.LayoutAction().WriteAutoRecoverySnapshot();
        if (!result.Ok()) {
            spdlog::warn("[autorecover] snapshot failed: {} ({})", result.detail, result.diagnostic);
            const std::string message = LayoutActionErrorMessage(result, state.localization);
            state.modePanels.SaveDataPanel().lastError = message;
            if (rmlui::RmlUiWorkspaceView* const workspace = state.rmlUiIntegration.WorkspaceView()) {
                workspace->Toasts().Post(ToastSeverity::Error, message);
            }
        }
    }
}

void SyncAndTickScene(FrameLoopState& loopState, FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;

    // --- シーン常駐土台: SceneRegistryとの差分反映+毎フレームの配置座標再計算 ---
    const std::chrono::steady_clock::time_point tSync0 = std::chrono::steady_clock::now();
    state.sceneRenderer.Sync(globals.app, state.sceneRegistry, state.loadLog);
    // スイッチ変更を反映するSyncの所要時間(=ユーザーが体感する引っかかり)を、
    // 変更した次のフレームでだけ実測して出す。改善の可否を目視の体感ではなく
    // 数値で判定するため(切替は変更フレームのSyncの後に行うので反映は次フレーム)。
    const double syncMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tSync0).count();
    loopState.switchFlipSmoke.MaybeLogMeasuredSync(loopState.frame, syncMs);
    loopState.sceneCycleSmoke.RecordSync(syncMs);

    // --- --switch-flip-smoke: スロット経路Sceneryのスイッチ値をGUI操作を模して変更する ---
    loopState.switchFlipSmoke.MaybeFlip(loopState.frame, state.sceneRegistry, state.sceneRenderer,
                                        state.loadLog);

    // --scene-cycle-smoke: Sceneを順に切り替えて、切替後のSyncの時間を測る。
    loopState.sceneCycleSmoke.MaybeSwitch(loopState.frame, state.LayoutAction(), state.layoutSession,
                                          state.sceneRenderer);
    if (loopState.sceneCycleSmoke.Finished()) PostQuitMessage(0);

    // 開いたプラグイン(一時的な配置物)の取り付け。Syncで載ったスロットのスイッチ確定(Tick)より前に行う。
    TickOpenedPlugin(globals, state);

    // Scene→graphアクセサ: アクティブSceneは
    // globals.editor.graphを返す。SwitchLayoutSceneはscenes[i].graphを切替時にのみ
    // 同期するため、切替後に加えた編集はeditor.graph側にしか無い。
    // 非アクティブSceneのみsession.scenes[i].graphを返す。
    // 非constポインタを返す。前進する
    // headがポイントを背向通過した際、TrainRuntime::TickGroupがここ経由で
    // activeSlotを書き戻すため。
    scratch.graphForScene = [&globals, &state](size_t sceneIndex) -> rs::world::TrackGraph* {
        if (sceneIndex == state.layoutSession.activeIndex) return &globals.editor.graph;
        if (sceneIndex < state.layoutSession.scenes.size()) return &state.layoutSession.scenes[sceneIndex].graph;
        return nullptr;
    };
    const bool requiresInitialTrainBatchTick = state.sceneRenderer.ConsumeInitialCarBatchTick();
    // 固定 tick がまだ来ていない初回フレームでも、遅延 upload した Train の
    // バッチを描画前に確立する。dt=0 なので走行・ゲーム時刻は進めない。
    scratch.sceneTicksThisFrame =
        requiresInitialTrainBatchTick && scratch.gameTicksThisFrame == 0 ? 1 : scratch.gameTicksThisFrame;
    // ゲーム内時計が1tickで進む秒。ダイヤの時刻の巻き戻しに使う。
    const double clockDtPerTick = scratch.gameTicksThisFrame == 0 ? 0.0 : scratch.gameDtPerTick;
    // ビューアーは時計を止めている(TimeScale=0)ので、可動部・アニメの再生は実時間(固定刻み)で進める。
    scratch.sceneDtPerTick = state.viewerMode && scratch.gameTicksThisFrame > 0 ? kFixedTimeStepSeconds
                                                                                : clockDtPerTick;
    // 列車物理+可動部アニメは上で計算したgameTicksThisFrame回、GameClockと
    // 同じgameDtPerTickで進める(時間加速時に列車の走行と可動部アニメが
    // GameClockと揃って早送りされる)。
    const std::chrono::steady_clock::time_point tSceneTick0 =
        loopState.frameProfiler.Enabled() ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point();
    for (int i = 0; i < scratch.sceneTicksThisFrame; ++i) {
        // GameClockはこのフレームぶんを既に全tick進め終えているため、ダイヤの発車時刻
        // 判定(rs::sim::DiaRuntime)へ渡す絶対ゲーム内秒は、このtickに対応する時刻まで
        // 巻き戻して渡す。両ループはtick回数と1tickの進み量が同じなので厳密に一致する
        // (gameTicksThisFrame==0の初回バッチtickではsceneDtPerTick==0で現在時刻になる)。
        const double tickAbsSeconds =
            globals.gameClock.AbsoluteSeconds() -
            static_cast<double>(scratch.sceneTicksThisFrame - 1 - i) * clockDtPerTick;
        state.sceneRenderer.Tick(globals.app, state.sceneRegistry, scratch.graphForScene,
                                 state.layoutSession.warpLinks, state.layoutSession.activeIndex,
                                 scratch.sceneDtPerTick, tickAbsSeconds, globals.editor.revision,
                                 globals.gameClock, scratch.sunState, scratch.solarLatitudeDeg,
                                 state.activePreset->nightThresholdSin,
                                 globals.cabinCamera.active ? globals.cabinCamera.followedTrainId
                                                             : rs::world::NilGuid(),
                                 scratch.eye, scratch.fieldOfView);
        scratch.recomputedSlotsThisFrame += state.sceneRenderer.LastTickRecomputedSlots();
        scratch.trainTickMsThisFrame += state.sceneRenderer.LastTrainTickMs();
        scratch.sceneryTickMsThisFrame += state.sceneRenderer.LastSceneryTickMs();
        scratch.Accumulate(state.sceneRenderer.LastTrainTickStats());
    }
    // 時間駆動アニメの反映はtick回数に依らず描画フレームごとに1回でよい。
    state.sceneRenderer.UpdateSceneAnimations(globals.app);
    const std::vector<rs::sim::WheelSoundEvent> wheelSoundEvents =
        SubmitTrainsAndCollectWheelSounds(loopState, scratch);
    // _CAMDISTや共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)を参照する静的配置は、スロット経路と同じ視点・視野角・
    // 共有状態の値(スロット経路のUpdateTimeSwitchesと同じ関数で求める)でバリアントを決め直す。
    const rs::world::SceneSharedSwitchValues sharedSwitches = rs::world::ComputeSceneSharedSwitchValues(
        globals.gameClock, scratch.sunState, scratch.solarLatitudeDeg, state.activePreset->nightThresholdSin,
        globals.app.ShadowsEnabled(), globals.app.EnvMapEnabled());
    state.sceneRenderer.UpdateBandedStaticInstances(globals.app, scratch.eye, scratch.fieldOfView,
                                                    sharedSwitches);
    const SceneTrainController::TickStats& renderSubmitStats =
        state.sceneRenderer.LastTrainRenderSubmitStats();
    scratch.trainSubmitMsThisFrame += renderSubmitStats.submitMs;
    scratch.carBatchMsThisFrame += renderSubmitStats.batchMs;
    scratch.carPartWorldMsThisFrame += renderSubmitStats.partWorldMs;
    scratch.trainRuntimeMsThisFrame += renderSubmitStats.runtimeMs;
    scratch.carFallbackMsThisFrame += renderSubmitStats.fallbackMs;
    if (loopState.frameProfiler.Enabled()) {
        scratch.sceneTickMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tSceneTick0)
                .count();
    }
    // AdvanceSimulationで既に解決済みの実効カメラ視線方向(カブ視点が有効なら運転台、
    // でなければ俯瞰カメラ)をそのまま使う(AdvanceSimulationはSyncAndTickSceneより
    // 前に呼ばれる、AppRuntime.cpp参照)。
    const float* listenerForward = scratch.forward;
    const float listenerUp[3] = {0.0f, 1.0f, 0.0f}; // FlyCameraはロール無しのため常にワールドUp
    // リスナー速度の分母は実フレーム時間×TimeScale(=このフレームで進んだゲーム内秒)。
    // 実行tick数×gameDtPerTickだと1/60秒の整数倍に量子化され、描画が60fpsを超える
    // 環境ではtick0のフレームで速度0、tickが来たフレームで数フレーム分の移動量を
    // 1tick分で割った尖った値、が交互に出てピッチが暴れる。分子(カメラ位置差分)と
    // 分母が同じ区間を測るようフレーム単位で揃える。一時停止はTimeScale=0なので
    // 分母が0になり速度0として扱われる。
    // 速度平滑化の時定数は聴感上の調整値なので、TimeScaleを掛けない実フレーム秒で渡す。
    // レイアウト読込・Scene切替のジョブ中は配置物のTickが止まり音源の位置が求まらないので、
    // 配置物の音を鳴らさない(空の一覧を渡すと鳴っている音も止まる)。ジョブが終わったフレームも
    // 鳴らさず、静的配置のインスタンスバッチの作り直しが済んだ次のフレームから鳴らす
    // (FrameLoopState::syncJobActiveLastFrame)。
    const bool placedSoundsMuted =
        state.sceneRenderer.SyncJobActive() || loopState.syncJobActiveLastFrame;
    if (loopState.placedSoundSceneGeneration != globals.editor.SceneGeneration()) {
        loopState.placedSoundDumpFrame = 0;
        loopState.placedSoundSceneGeneration = globals.editor.SceneGeneration();
    }
    const std::vector<rs::sim::PlacedSoundRequest> placedSoundRequests =
        placedSoundsMuted ? std::vector<rs::sim::PlacedSoundRequest>()
                          : CollectPlacedSoundRequests(state.sceneRenderer.Slots(), loopState.placedSoundDumpFrame);
    // 灯具のレンズフレア。音源と同じく、読込ジョブ中は出さない(空の一覧)。
    StaticHeadlightSource staticHeadlightSource;
    staticHeadlightSource.roster = &state.sceneRenderer.StaticHeadlights();
    staticHeadlightSource.graph = &globals.editor.graph;
    staticHeadlightSource.findEntity = [&state](const rs::world::Guid& entityId,
                                                bool signalModel) -> const rs::world::SceneEntity* {
        return signalModel ? state.sceneRenderer.SignalModels().FindEntity(entityId)
                           : state.sceneRegistry.FindEntity(entityId);
    };
    // 上で静的配置の組を決め直した値(視点・視野角・共有状態)と同じものを渡す。
    staticHeadlightSource.sharedState = sharedSwitches;
    staticHeadlightSource.hasView = true;
    std::copy(scratch.eye, scratch.eye + 3, staticHeadlightSource.eye);
    staticHeadlightSource.fovYRadians = scratch.fieldOfView;
    globals.app.SetHeadlightLights(
        placedSoundsMuted
            ? std::vector<rs::render::HeadlightLight>()
            : state.headlightCollector.Collect(
                  state.sceneRenderer.Slots(),
                  staticHeadlightSource,
                  state.flareTextureSlots, globals.app.LensFlare()));
    state.audioEngine.SyncPlacedSounds(placedSoundRequests,
                                       scratch.eye, listenerForward, listenerUp,
                                       static_cast<double>(loopState.io.DeltaTime) *
                                           globals.gameClock.TimeScale(),
                                       static_cast<double>(loopState.io.DeltaTime));
    // 車輪音は配置物の音と同じく、読込ジョブ中は鳴らさない。リスナー位置はSyncPlacedSoundsで更新済み。
    if (!placedSoundsMuted) state.audioEngine.PlayWheelSounds(wheelSoundEvents);
}

} // namespace rs::app

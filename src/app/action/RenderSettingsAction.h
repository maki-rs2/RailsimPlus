//
// RenderSettingsAction.h - 描画設定のアプリケーション操作
//
#pragma once

#include "../settings/UserSettings.h"

namespace rs::app {

class App;
class CameraController;

struct RenderSettingsRefs {
    App& app;
    CameraController& camera;
    UserSettingsData& userSettings;
};

struct RenderSettingsViewModel {
    float renderDistance = 0.0f;
    bool atmosphericPerspective = true;  // 設定値(大気遠近を使うか)
    bool heightFog = true;               // 設定値(高さフォグを使うか)
    int tonemapMode = 0;              // TonemapModeの値
    int shadowQualityIndex = 0;       // kShadowQualityChoicesの添字(画面の選択肢の並びと同じ)
    int shadowDistanceIndex = 0;      // kShadowDistanceChoicesの添字(同上)
    bool sunLensFlare = true;         // 太陽のフレア
    bool sunWhiteout = true;          // 太陽の白飛び
    bool miscLensFlare = true;        // 灯具のフレア
    bool miscParticle = true;
    bool bloom = true;                // ブルーム
    bool motionBlur = false;          // モーションブラー
    bool volumetricFog = false;       // ボリュメトリックフォグ
    int antiAliasingIndex = 0;        // kAntiAliasingChoicesの添字(画面の選択肢の並びと同じ)
    int ambientOcclusionIndex = 0;    // kAoQualityChoicesの添字(同上)
    bool vsync = true;
    int fpsLimitIndex = 0;            // kFpsLimitChoicesの添字(同上)
};

// 保存済みの描画設定(空・影・レンズフレア・VSync・FPS上限)を描画側へ渡す。起動時に1回呼ぶ。
void ApplyRenderSettings(App& app, const UserSettingsData& settings);
void ApplyRuntimeSettingOverrides(App& app, const UserSettingsData& settings,
                                  const std::vector<std::string>& overrides);
// セーフモードで描画側へ渡す設定。settingsの描画の項目を低品質へ下げたコピーを返す。settings本体は
// 変更のたびにファイルへ保存されるので、書き換えない(セーフモードは設定を恒久に変えない)。
UserSettingsData MakeSafeModeRenderSettings(const UserSettingsData& settings);

class RenderSettingsAction {
public:
    explicit RenderSettingsAction(const RenderSettingsRefs& refs);

    // 描画距離[m]の選べる範囲(スライダーの両端)。
    static float RenderDistanceMin();
    static float RenderDistanceMax();
    void SetRenderDistance(float distance);
    void SetAtmosphericPerspective(bool enabled);
    void SetHeightFog(bool enabled);
    void SetTonemapMode(int mode);
    // choiceIndexはkShadowQualityChoicesの添字。範囲外は無視する。
    void SetShadowQualityIndex(int choiceIndex);
    // choiceIndexはkShadowDistanceChoicesの添字。範囲外は無視する。
    void SetShadowDistanceIndex(int choiceIndex);
    // 太陽のフレア・太陽の白飛び・灯具のフレアのオン・オフ。
    void SetSunLensFlare(bool enabled);
    void SetSunWhiteout(bool enabled);
    void SetMiscLensFlare(bool enabled);
    void SetMiscParticle(bool enabled);
    void SetBloom(bool enabled);
    void SetMotionBlur(bool enabled);
    void SetVolumetricFog(bool enabled);
    // choiceIndexはkAntiAliasingChoicesの添字。範囲外は無視する。
    void SetAntiAliasingIndex(int choiceIndex);
    // choiceIndexはkAoQualityChoicesの添字。範囲外は無視する。
    void SetAmbientOcclusionIndex(int choiceIndex);
    void SetVSync(bool enabled);
    // choiceIndexはkFpsLimitChoicesの添字。範囲外は無視する。
    void SetFpsLimitIndex(int choiceIndex);
    RenderSettingsViewModel ViewModel() const;

private:
    // 保存済みの設定から3つのスイッチをまとめて描画側へ渡し、保存する。
    void ApplyLensFlareSwitches();

    RenderSettingsRefs refs_;
};

} // namespace rs::app

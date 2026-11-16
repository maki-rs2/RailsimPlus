//
// PluginSwitchTable.h - プラグインのユーザー/システムスイッチと解決状態を保持する
//
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../plugins/load/PluginLoader.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"
#include "../../world/time/SceneSharedSwitches.h"

namespace rs::app {

struct SwitchEvaluationInput {
    std::map<std::string, int32_t> userSwitches;
    std::map<std::string, int32_t> systemSwitches;
    std::map<std::string, int32_t> routineSwitchOverrides;
    std::optional<int32_t> camDistBand;
    rs::world::SceneSharedSwitchValues sharedState;

    int32_t ResolveSwitch(const std::string& name) const;
};

class PluginSwitchTable {
public:
    std::map<std::string, int32_t> userSwitches;   // DefineSwitch 由来(初期0)
    std::map<std::string, int32_t> systemSwitches; // "_名前" 参照から自動収集(初期0)
    std::map<std::string, int32_t> routineSwitchOverrides;
    uint64_t switchGeneration = 0;
    float dayAlpha = 1.0f;

    void CollectSwitchTables(const rs::legacy_pi::PluginDefinition& def);
    void CollectCamDistBands(const rs::legacy_pi::PluginDefinition& def);
    void RefreshSwitchGeneration();
    int32_t ResolveSwitch(const std::string& name) const;
    SwitchEvaluationInput BuildEvaluationInput(
        const rs::legacy_pi::PluginDefinition& def,
        const std::vector<int32_t>* switchOptions = nullptr,
        std::optional<int32_t> camDist = std::nullopt,
        std::optional<rs::world::SceneSharedSwitchValues> sharedState = std::nullopt) const;
    void ApplyRoutineSwitchOverrides(const std::map<std::string, int32_t>& overrides);
    const std::vector<int32_t>& CamDistBandStarts() const { return camDistBandStarts_; }
    bool CamDistQuantizationSafe() const { return camDistQuantizationSafe_; }
    void UpdateCamDist(const float eye[3], const float* unitOrigin, float viewerFovYRadians);
    // プラグインが`_CAMDIST`を参照している(systemSwitchesへ登録済み)か。
    bool ReferencesCamDist() const;
    void UpdateTimeSwitches(const rs::world::GameClock& clock, const rs::world::SolarState& sun,
                            double latitudeDeg, double nightThresholdSin, bool shadowEnabled,
                            bool envMapEnabled, bool writeCalendar = true);
    void UpdateTrainSwitches(double velocityKmh, double accelMhPerS, bool front,
                             bool connectFront, bool connectRear, int serial);
    bool UpdateStationSwitches(int32_t approach1, int32_t approach2, int32_t stopping);

private:
    std::vector<int32_t> camDistBandStarts_;
    bool camDistQuantizationSafe_ = true;
    std::map<std::string, int32_t> lastUserSwitches_;
    std::map<std::string, int32_t> lastSystemSwitches_;
    std::map<std::string, int32_t> lastRoutineSwitchOverrides_;
    int32_t lastDayAlphaQuantized_ = 255;
    bool switchGenerationInit_ = false;
};

} // namespace rs::app

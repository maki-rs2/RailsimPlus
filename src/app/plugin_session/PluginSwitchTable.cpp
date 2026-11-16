//
// PluginSwitchTable.cpp - PluginSwitchTable のスイッチ解決実装
//
#include "PluginSwitchTable.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <iterator>
#include <numbers>
#include <regex>
#include <string>

#include <spdlog/spdlog.h>

#include "../../plugins/schema/CamDistBands.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../plugins/schema/SwitchExpr.h"
#include "../../plugins/schema/SystemSwitchAxes.h"
#include "../../world/time/SceneSharedSwitches.h"
#include "SharedStateAxes.h"

namespace rs::app {

namespace {
int32_t ResolveSwitchValue(const std::string& name,
                           const std::map<std::string, int32_t>& userSwitches,
                           const std::map<std::string, int32_t>& systemSwitches,
                           const std::map<std::string, int32_t>& overrides) {
    for (const std::map<std::string, int32_t>* values : {&overrides, &userSwitches, &systemSwitches}) {
        const std::map<std::string, int32_t>::const_iterator it = values->find(name);
        if (it != values->end()) return it->second;
    }
    return 0;
}
} // namespace

int32_t SwitchEvaluationInput::ResolveSwitch(const std::string& name) const {
    return ResolveSwitchValue(name, userSwitches, systemSwitches, routineSwitchOverrides);
}

SwitchEvaluationInput PluginSwitchTable::BuildEvaluationInput(
    const rs::legacy_pi::PluginDefinition& def, const std::vector<int32_t>* switchOptions,
    std::optional<int32_t> camDist,
    std::optional<rs::world::SceneSharedSwitchValues> sharedState) const {
    SwitchEvaluationInput input;
    input.userSwitches = userSwitches;
    input.systemSwitches = systemSwitches;
    input.routineSwitchOverrides = routineSwitchOverrides;
    if (switchOptions) {
        // 保存値が短くても、再利用前の末尾の値を残さない。
        for (size_t i = 0; i < def.switches.size(); ++i)
            input.userSwitches[def.switches[i].name] =
                i < switchOptions->size() ? (*switchOptions)[i] : 0;
    }
    const auto camIt = input.systemSwitches.find(
        rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist));
    if (camIt != input.systemSwitches.end()) {
        input.camDistBand = rs::legacy_pi::QuantizeCamDistToBand(
            camDist.value_or(camIt->second), camDistBandStarts_, camDistQuantizationSafe_);
        camIt->second = *input.camDistBand;
    }
    if (sharedState) WriteSharedStateSwitches(input.systemSwitches, *sharedState);
    for (const SharedStateAxisEntry& axis : kSharedStateAxes) {
        const auto it = input.systemSwitches.find(rs::legacy_pi::SystemSwitchName(axis.id));
        if (it != input.systemSwitches.end()) input.sharedState.*(axis.member) = it->second;
    }
    return input;
}

void PluginSwitchTable::CollectSwitchTables(const rs::legacy_pi::PluginDefinition& def) {
    // ユーザースイッチ(DefineSwitch)テーブルを初期化(0)
    userSwitches.clear();
    for (const auto& sw : def.switches) userSwitches[sw.name] = 0;

    // システムスイッチ("_名前" 参照)を customizers の trig.chain の各段
    // (condition/switchName)文字列から正規表現で自動収集する。ネストした
    // If/ApplySwitchの祖先段も含め、chain全体を舐める必要がある
    // （1段しか見ないと、外側のIf/ApplySwitchだけが参照する_始まりスイッチが
    // UIのスイッチパネルに出なくなる新規リグレッションになるため）。
    systemSwitches.clear();
    static const std::regex kSysNameRe("_[A-Za-z0-9\\x80-\\xFF]+");
    auto scanForSystemNames = [&](const std::string& s) {
        for (auto it = std::sregex_iterator(s.begin(), s.end(), kSysNameRe);
             it != std::sregex_iterator(); ++it) {
            std::string name = it->str();
            if (systemSwitches.find(name) == systemSwitches.end()) systemSwitches[name] = 0;
        }
    };
    auto scanChain = [&](const std::vector<rs::legacy_pi::TriggerStep>& chain) {
        for (const auto& step : chain) {
            if (step.trigger == rs::legacy_pi::TriggerKind::Condition) {
                scanForSystemNames(step.condition);
            } else if (step.switchIsExpression) {
                // ApplySwitch ("_YEAR"/10)%10 のように式のラベルも、中の"_名前"を拾う
                scanForSystemNames(step.switchName);
            } else if (!step.switchName.empty() && step.switchName[0] == '_') {
                if (systemSwitches.find(step.switchName) == systemSwitches.end())
                    systemSwitches[step.switchName] = 0;
            }
        }
    };
    // customizers(可動部)だけでなく、ChangeModel/ChangeTexture/ChangeMaterial/
    // SetAnimation/texture-transformer/ChangeAlphaのtrig.chainも走査対象に含める。
    // ここで拾わないと、可動部を持たず上記だけで_始まりスイッチを参照する
    // プラグイン(_VELOCITY等で見た目を切り替えるだけの車両プラグイン等)で、
    // そのスイッチ名がsystemSwitchesに登録されないままとなり、
    // UpdateTrainSwitches等のsetIfReferencedが無言でno-opし続ける
    // (=値を書いても永久に反映されない)ため。
    for (const auto& part : def.parts) {
        for (const auto& c : part.customizers) scanChain(c.trig.chain);
        for (const auto& c : part.changeModels) scanChain(c.trig.chain);
        for (const auto& c : part.changeMaterials) scanChain(c.trig.chain);
        for (const rs::legacy_pi::TextureStep& step : part.textureSteps) scanChain(step.Trig().chain);
        for (const auto& c : part.changeAlphas) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.envMaps) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.bumpMaps) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.alphaZeroTests) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.transparents) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.noCastShadows) scanChain(c.trig.chain);
        for (const rs::legacy_pi::MaterialFlagCustomizer& c : part.noReceiveShadows) scanChain(c.trig.chain);
    }
    // SoundEffectの発動条件も同じ理由で走査する。音源の鳴り分けだけで速度・加速度の
    // システムスイッチを参照するプラグインでは、ここで拾わないとそのスイッチが
    // 未登録のままとなり、値を書いても常に0のままで条件が成立しない。
    for (const auto& se : def.soundEffects) scanChain(se.trig.chain);
    // 灯具の発動条件も同じ。テールランプの出し分け(_FRONT等)だけでスイッチを参照するプラグインがある。
    for (const rs::legacy_pi::HeadlightDef& headlight : def.headlights) scanChain(headlight.trig.chain);
    // パーティクルの発動条件も同じ(蒸気・煙は _VELOCITY などだけで出し分ける)。
    for (const rs::legacy_pi::ParticleDef& particle : def.particles) scanChain(particle.trig.chain);

    // 表(kSystemSwitchAxes)にも DefineSwitch にも無い"_名前"は、値を供給する経路が無く
    // 常に0のままになる。綴り違いか、本アプリが対応していないRailSim IIのスイッチなので警告する。
    // 静的インスタンス化の判定は変えない(スロット経路でも値は0のままで見た目が同じ)。
    std::string unknownNames;
    for (const auto& [name, value] : systemSwitches) {
        if (rs::legacy_pi::FindSystemSwitchAxis(name) != nullptr) continue;
        if (userSwitches.find(name) != userSwitches.end()) continue;
        if (!unknownNames.empty()) unknownNames += ", ";
        unknownNames += name;
    }
    if (!unknownNames.empty())
        spdlog::warn("未知のシステムスイッチ名を参照しています(値は常に0): {}", unknownNames);
}

void PluginSwitchTable::CollectCamDistBands(const rs::legacy_pi::PluginDefinition& def) {
    camDistBandStarts_.clear();
    camDistQuantizationSafe_ = true;

    const auto collectChain = [this](const rs::legacy_pi::TriggerInfo& trigger) {
        for (const rs::legacy_pi::TriggerStep& step : trigger.chain) {
            if (step.trigger == rs::legacy_pi::TriggerKind::Condition) {
                const rs::legacy_pi::CamDistConditionAnalysis analysis =
                    rs::legacy_pi::AnalyzeCamDistCondition(step.condition);
                if (analysis.kind == rs::legacy_pi::CamDistConditionKind::Unsupported)
                    camDistQuantizationSafe_ = false;
                if (analysis.kind == rs::legacy_pi::CamDistConditionKind::Supported)
                    camDistBandStarts_.insert(camDistBandStarts_.end(), analysis.bandStarts.begin(),
                                              analysis.bandStarts.end());
            } else if (step.switchIsExpression) {
                // 式のラベル(ApplySwitch "_CAMDIST"/100 等)は Case 値から段の境目を
                // 求められないので、`_CAMDIST`を含むなら量子化しない
                if (rs::legacy_pi::ExprReferencesSwitch(
                        step.switchName,
                        rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist)))
                    camDistQuantizationSafe_ = false;
            } else if (step.switchName == rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist)) {
                for (const int caseValue : step.caseValues) {
                    if (caseValue < 0) continue;
                    camDistBandStarts_.push_back(caseValue);
                    camDistBandStarts_.push_back(caseValue == INT_MAX ? INT_MAX : caseValue + 1);
                }
            }
        }
    };
    for (const rs::legacy_pi::ModelPart& part : def.parts) {
        for (const rs::legacy_pi::StaticCustomizer& value : part.customizers) collectChain(value.trig);
        for (const rs::legacy_pi::ChangeModelRef& value : part.changeModels) collectChain(value.trig);
        for (const rs::legacy_pi::ChangeMaterialCustomizer& value : part.changeMaterials) collectChain(value.trig);
        for (const rs::legacy_pi::TextureStep& step : part.textureSteps) collectChain(step.Trig());
        for (const rs::legacy_pi::ChangeAlphaCustomizer& value : part.changeAlphas) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.envMaps) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.bumpMaps) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.alphaZeroTests) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.transparents) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.noCastShadows) collectChain(value.trig);
        for (const rs::legacy_pi::MaterialFlagCustomizer& value : part.noReceiveShadows) collectChain(value.trig);
    }
    camDistBandStarts_.push_back(0);
    std::sort(camDistBandStarts_.begin(), camDistBandStarts_.end());
    camDistBandStarts_.erase(std::unique(camDistBandStarts_.begin(), camDistBandStarts_.end()),
                             camDistBandStarts_.end());
}

void PluginSwitchTable::RefreshSwitchGeneration() {
    const int32_t dayAlphaQuantized = static_cast<int32_t>(std::lround(dayAlpha * 255.0f));
    if (!switchGenerationInit_ || userSwitches != lastUserSwitches_ ||
        systemSwitches != lastSystemSwitches_ ||
        routineSwitchOverrides != lastRoutineSwitchOverrides_ ||
        dayAlphaQuantized != lastDayAlphaQuantized_) {
        ++switchGeneration;
        lastUserSwitches_ = userSwitches;
        lastSystemSwitches_ = systemSwitches;
        lastRoutineSwitchOverrides_ = routineSwitchOverrides;
        lastDayAlphaQuantized_ = dayAlphaQuantized;
        switchGenerationInit_ = true;
    }
}

int32_t PluginSwitchTable::ResolveSwitch(const std::string& name) const {
    return ResolveSwitchValue(name, userSwitches, systemSwitches, routineSwitchOverrides);
}

void PluginSwitchTable::ApplyRoutineSwitchOverrides(const std::map<std::string, int32_t>& overrides) {
    routineSwitchOverrides = overrides;
}

void PluginSwitchTable::UpdateCamDist(const float eye[3], const float* unitOrigin,
                                      float viewerFovYRadians) {
    auto it = systemSwitches.find(
        rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist));
    if (it == systemSwitches.end()) return; // プラグインが参照していない

    // unitOrigin省略時はワールド原点を基準とする(ビューア用途)。
    constexpr float kWorldOrigin[3] = {0.0f, 0.0f, 0.0f};
    const int32_t rawCamDist = rs::legacy_pi::ComputeScaledCamDistance(
        eye, unitOrigin ? unitOrigin : kWorldOrigin, viewerFovYRadians);
    it->second = rs::legacy_pi::QuantizeCamDistToBand(rawCamDist, camDistBandStarts_,
                                                      camDistQuantizationSafe_);
}

bool PluginSwitchTable::ReferencesCamDist() const {
    return systemSwitches.find(
               rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist)) !=
           systemSwitches.end();
}

void PluginSwitchTable::UpdateTimeSwitches(const rs::world::GameClock& clock,
                                           const rs::world::SolarState& sun, double latitudeDeg,
                                           double nightThresholdSin, bool shadowEnabled,
                                           bool envMapEnabled, bool writeCalendar) {
    dayAlpha = static_cast<float>(rs::world::ComputeDayAlpha(sun.directionToSunY, nightThresholdSin));
    auto setIfReferenced = [&](const char* name, int32_t value) {
        auto it = systemSwitches.find(name);
        if (it != systemSwitches.end()) it->second = value;
    };

    // 日付・時刻・季節を書かない呼び出し(プラグインビューアー)は、これらを設定の値のまま保つ。
    // 昼夜係数・_NIGHT・_SHADOW・_ENVMAPは、書く呼び出しと同じ値を書く。
    if (writeCalendar) {
        setIfReferenced("_YEAR", clock.Year());
        setIfReferenced("_MONTH", clock.Month());
        setIfReferenced("_DAY", clock.Day());
        setIfReferenced("_DAYOFWEEK", clock.DayOfWeek()); // 0=日曜〜6=土曜
        setIfReferenced("_HOUR", clock.Hour());
        setIfReferenced("_MINUTE", clock.Minute());
        setIfReferenced("_SECOND", clock.Second());
    }
    // _NIGHT/_SEASON/_SHADOW/_ENVMAPは静的インスタンス経路(バリアント切り替え)と同じ関数で求める
    // (directionToSunYがsin値であること等の仕様はComputeSceneSharedSwitchValues参照)。
    rs::world::SceneSharedSwitchValues shared = rs::world::ComputeSceneSharedSwitchValues(
        clock, sun, latitudeDeg, nightThresholdSin, shadowEnabled, envMapEnabled);
    if (!writeCalendar) {
        const auto seasonIt = systemSwitches.find("_SEASON");
        if (seasonIt != systemSwitches.end()) shared.season = seasonIt->second;
    }
    WriteSharedStateSwitches(systemSwitches, shared);
}

void PluginSwitchTable::UpdateTrainSwitches(double velocityKmh, double accelMhPerS, bool front,
                                            bool connectFront, bool connectRear, int serial) {
    auto setIfReferenced = [&](const char* name, int32_t value) {
        auto it = systemSwitches.find(name);
        if (it != systemSwitches.end()) it->second = value;
    };
    setIfReferenced("_VELOCITY", static_cast<int32_t>(std::lround(std::fabs(velocityKmh))));
    setIfReferenced("_ACCEL", static_cast<int32_t>(std::lround(accelMhPerS)));
    setIfReferenced("_FRONT", front ? 1 : 0);
    setIfReferenced("_CONNECT1", connectFront ? 1 : 0);
    setIfReferenced("_CONNECT2", connectRear ? 1 : 0);
    setIfReferenced("_SERIAL", serial);
}

bool PluginSwitchTable::UpdateStationSwitches(int32_t approach1, int32_t approach2,
                                              int32_t stopping) {
    bool changed = false;
    auto setIfReferenced = [&](const char* name, int32_t value) {
        std::map<std::string, int32_t>::iterator it = systemSwitches.find(name);
        if (it == systemSwitches.end() || it->second == value) return;
        it->second = value;
        changed = true;
    };
    setIfReferenced("_APPROACH1", approach1);
    setIfReferenced("_APPROACH2", approach2);
    setIfReferenced("_STOPPING", stopping);
    return changed;
}

} // namespace rs::app

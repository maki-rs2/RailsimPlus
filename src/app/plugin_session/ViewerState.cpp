#include "ViewerState.h"

#include <array>
#include <map>
#include <string>

#include "../../plugins/schema/SystemSwitchAxes.h"
#include "PluginSession.h"

namespace rs::app {

namespace {

using rs::legacy_pi::SystemSwitchId;

// _NIGHT の値(1=夜)。
constexpr std::int32_t kNightOff = 0;
constexpr std::int32_t kNightOn = 1;

struct InitialSwitch {
    SystemSwitchId id;
    std::int32_t value;
};

// 読込時に入れる日付・時刻・季節の初期値。日付・時刻はRailSim IIのビューアーと同じ。
// _SEASON は RailSim II のビューアーが季節を設定しないため初期値の0。
constexpr std::array<InitialSwitch, 8> kInitialSwitches = {{
    {SystemSwitchId::Year, 1},
    {SystemSwitchId::Month, 1},
    {SystemSwitchId::Day, 1},
    {SystemSwitchId::DayOfWeek, 0},
    {SystemSwitchId::Hour, 0},
    {SystemSwitchId::Minute, 0},
    {SystemSwitchId::Second, 0},
    {SystemSwitchId::Season, 0},
}};

// プラグインが参照しているシステムスイッチにだけ値を書く。
void WriteIfReferenced(PluginSession& session, SystemSwitchId id, std::int32_t value) {
    std::map<std::string, std::int32_t>& switches = session.SystemSwitches();
    const std::map<std::string, std::int32_t>::iterator it =
        switches.find(rs::legacy_pi::SystemSwitchName(id));
    if (it != switches.end()) it->second = value;
}

rs::world::SolarOverrides OverridesFor(bool lightOn) {
    rs::world::SolarOverrides overrides;
    overrides.earthRotationMode = lightOn ? rs::world::EarthRotationMode::AlwaysDay : rs::world::EarthRotationMode::AlwaysNight;
    return overrides;
}

std::int32_t NightValueFor(bool lightOn) {
    return lightOn ? kNightOff : kNightOn;
}

} // namespace

ViewerState::ViewerState() : solarOverrides(OverridesFor(true)) {}

void SetViewerLight(ViewerState& viewer, PluginSession& session, bool lightOn) {
    viewer.lightOn = lightOn;
    viewer.solarOverrides = OverridesFor(lightOn);
    WriteIfReferenced(session, SystemSwitchId::Night, NightValueFor(lightOn));
}

void ApplyViewerInitialSwitches(const ViewerState& viewer, PluginSession& session) {
    for (const InitialSwitch& initial : kInitialSwitches) {
        WriteIfReferenced(session, initial.id, initial.value);
    }
    WriteIfReferenced(session, SystemSwitchId::Night, NightValueFor(viewer.lightOn));
}

} // namespace rs::app

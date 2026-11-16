//
// SystemSwitchAxes.h - RailSim2のシステムスイッチ(_NIGHT/_CAMDIST等)の「軸ごとの性質」の表
//
// システムスイッチ25個それぞれの範囲・取りうる値・値の出どころ・静的インスタンス化の
// 扱いを kSystemSwitchAxes の1か所に集める。判定(静的インスタンス化の除外・UIの読取専用・
// 駅舎の実行時スイッチ一覧)は名前の比較ではなく、この表を読む関数で行う。
// スイッチを足すときは SystemSwitchId と表の行を足す(static_assertが並びと件数を確認する)。
// 値は RailSim II の CModelSwitch.cpp InitSystemSwitch と公式ヘルプ pi_sym_model_switch.html に基づく。
//
// 駅舎の実行時スイッチ(source == StationRuntime: _APPROACH1/_APPROACH2/_STOPPING):
// 情景(Scenery)配置は読込時のスイッチ確定値(=スロットのcontentKey)で見た目を焼き込むため、
// contentKeyに含まれないこれらが「配置後に値が変わりうるスイッチ」になる。
// テクスチャの事前ロード対象を「これらが取りうる値の下でactiveになりうる集合」まで広げる
// ために、CollectPreloadTextures(PreloadTextureSet.h)が仮定値の列挙に使う。
// 仮定値は「全ビット0 / 全ビット1」の2通り: 値は番線ごとのビットマスクなので、
// 全ビット1にすればビット位置に依存する条件(`("_STOPPING">>n)&1`等)もビット位置に
// 関わらず真にできる。_CAMDISTも同じ性質を持つが、取りうる値はビットではなく
// バンド開始値の集合なので CamDistBandStarts() を使う。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rs::legacy_pi {

enum class SystemSwitchId : int32_t {
    Front,
    Connect1,
    Connect2,
    Door1,
    Door2,
    Serial,
    CamDist,
    Velocity,
    Accel,
    CabinView,
    Approach1,
    Approach2,
    Stopping,
    Night,
    Weather,
    Season,
    Shadow,
    EnvMap,
    Year,
    Month,
    Day,
    DayOfWeek,
    Hour,
    Minute,
    Second,
    Count_, // 件数(表の長さの確認用。スイッチではない)
};

inline constexpr size_t kSystemSwitchCount = static_cast<size_t>(SystemSwitchId::Count_);

// スイッチがどの単位で値を持つか。
enum class SystemSwitchScope { Scene, Placement, Station, Formation, Vehicle };

// 値の取りうる形。domainMin/domainMax は Binary/Enum のときだけ意味を持つ(他は0)。
enum class SystemSwitchDomain { Binary, Enum, Continuous, Bitmask };

// 実行時の値の出どころ。None は本アプリが値を供給していない。
enum class SystemSwitchSource {
    Clock,
    Camera,
    Formation,
    Vehicle,
    StationRuntime,
    Settings,
    None
};

// 静的インスタンス化(共有マテリアル状態への固定)との相性。
// Dynamic は静的インスタンス化から除外する(インスタンス間で値を共有できない、または
// 静的に焼いた後に値を更新する経路が無い)。
// StaticByBand は値が配置ごとに変わるが、バンド開始値へ量子化すれば有限個の状態として
// 静的に焼ける(_CAMDIST)。
// StaticBySharedState は値がシーン内の全配置で同じで、取りうる値が少数(domainMin〜domainMax)。
// 値ごとに焼いておき、値が変わったらバッチのメッシュを切り替える(_NIGHT/_SEASON/_SHADOW/_ENVMAP)。
enum class SystemSwitchStaticVerdict { StaticOk, Dynamic, StaticByBand, StaticBySharedState, Count_ };

inline constexpr size_t kSystemSwitchStaticVerdictCount =
    static_cast<size_t>(SystemSwitchStaticVerdict::Count_);

struct SystemSwitchAxis {
    SystemSwitchId id;
    const char* name;
    SystemSwitchScope scope;
    SystemSwitchDomain domain;
    int32_t domainMin;
    int32_t domainMax;
    SystemSwitchSource source;
    bool implemented;
    bool userEditable;
    SystemSwitchStaticVerdict staticVerdict;
};

// 列挙と同じ順序の表。_APPROACH1 -> _APPROACH2 -> _STOPPING が連続する並びは
// PreloadTextureSet の仮定値の反復順に使われる。
inline constexpr std::array<SystemSwitchAxis, kSystemSwitchCount> kSystemSwitchAxes = {{
    {SystemSwitchId::Front, "_FRONT", SystemSwitchScope::Vehicle, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Vehicle, true, true, SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Connect1, "_CONNECT1", SystemSwitchScope::Vehicle,
     SystemSwitchDomain::Binary, 0, 1, SystemSwitchSource::Vehicle, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Connect2, "_CONNECT2", SystemSwitchScope::Vehicle,
     SystemSwitchDomain::Binary, 0, 1, SystemSwitchSource::Vehicle, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Door1, "_DOOR1", SystemSwitchScope::Vehicle, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Vehicle, true, true, SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Door2, "_DOOR2", SystemSwitchScope::Vehicle, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Vehicle, true, true, SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Serial, "_SERIAL", SystemSwitchScope::Formation,
     SystemSwitchDomain::Continuous, 0, 0, SystemSwitchSource::Formation, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::CamDist, "_CAMDIST", SystemSwitchScope::Placement,
     SystemSwitchDomain::Continuous, 0, 0, SystemSwitchSource::Camera, true, false,
     SystemSwitchStaticVerdict::StaticByBand},
    {SystemSwitchId::Velocity, "_VELOCITY", SystemSwitchScope::Formation,
     SystemSwitchDomain::Continuous, 0, 0, SystemSwitchSource::Formation, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Accel, "_ACCEL", SystemSwitchScope::Formation,
     SystemSwitchDomain::Continuous, 0, 0, SystemSwitchSource::Formation, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::CabinView, "_CABINVIEW", SystemSwitchScope::Vehicle,
     SystemSwitchDomain::Binary, 0, 1, SystemSwitchSource::Vehicle, true, true,
     SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Approach1, "_APPROACH1", SystemSwitchScope::Station,
     SystemSwitchDomain::Bitmask, 0, 0, SystemSwitchSource::StationRuntime, true, true,
     SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Approach2, "_APPROACH2", SystemSwitchScope::Station,
     SystemSwitchDomain::Bitmask, 0, 0, SystemSwitchSource::StationRuntime, true, true,
     SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Stopping, "_STOPPING", SystemSwitchScope::Station,
     SystemSwitchDomain::Bitmask, 0, 0, SystemSwitchSource::StationRuntime, true, true,
     SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Night, "_NIGHT", SystemSwitchScope::Scene, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::StaticBySharedState},
    {SystemSwitchId::Weather, "_WEATHER", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 0, 0,
     SystemSwitchSource::None, false, true, SystemSwitchStaticVerdict::StaticOk},
    {SystemSwitchId::Season, "_SEASON", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 0, 3,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::StaticBySharedState},
    {SystemSwitchId::Shadow, "_SHADOW", SystemSwitchScope::Scene, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Settings, true, true,
     SystemSwitchStaticVerdict::StaticBySharedState},
    {SystemSwitchId::EnvMap, "_ENVMAP", SystemSwitchScope::Scene, SystemSwitchDomain::Binary, 0, 1,
     SystemSwitchSource::Settings, true, true,
     SystemSwitchStaticVerdict::StaticBySharedState},
    {SystemSwitchId::Year, "_YEAR", SystemSwitchScope::Scene, SystemSwitchDomain::Continuous, 0, 0,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Month, "_MONTH", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 1, 12,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Day, "_DAY", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 1, 31,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::DayOfWeek, "_DAYOFWEEK", SystemSwitchScope::Scene, SystemSwitchDomain::Enum,
     0, 6, SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Hour, "_HOUR", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 0, 23,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Minute, "_MINUTE", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 0, 59,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
    {SystemSwitchId::Second, "_SECOND", SystemSwitchScope::Scene, SystemSwitchDomain::Enum, 0, 59,
     SystemSwitchSource::Clock, true, true, SystemSwitchStaticVerdict::Dynamic},
}};

namespace system_switch_detail {

constexpr bool AxesAreConsistent() {
    for (size_t i = 0; i < kSystemSwitchAxes.size(); ++i) {
        const SystemSwitchAxis& axis = kSystemSwitchAxes[i];
        if (axis.id != static_cast<SystemSwitchId>(i)) return false;
        // 値の出どころがないスイッチは実装済みにできない。逆は成り立たない(出どころがあっても実装済みでないスイッチがある)。
        if (axis.source == SystemSwitchSource::None && axis.implemented) return false;
    }
    return true;
}

} // namespace system_switch_detail

static_assert(kSystemSwitchAxes.size() == static_cast<size_t>(SystemSwitchId::Count_),
              "表の件数が列挙と一致していません");
static_assert(system_switch_detail::AxesAreConsistent(),
              "表の並びが列挙と違う、またはsourceがNoneなのにimplementedになっています");

inline const SystemSwitchAxis& SystemSwitchAxisOf(SystemSwitchId id) {
    return kSystemSwitchAxes[static_cast<size_t>(id)];
}

constexpr const char* SystemSwitchName(SystemSwitchId id) {
    return kSystemSwitchAxes[static_cast<size_t>(id)].name;
}

// 表にない名前は nullptr(通常のユーザースイッチ名など)。
inline const SystemSwitchAxis* FindSystemSwitchAxis(std::string_view name) {
    for (const SystemSwitchAxis& axis : kSystemSwitchAxes) {
        if (name == axis.name) return &axis;
    }
    return nullptr;
}

// UIから値を書き換えてよいか。表にない名前は true。
inline bool IsSystemSwitchUserEditable(std::string_view name) {
    const SystemSwitchAxis* axis = FindSystemSwitchAxis(name);
    return axis == nullptr || axis->userEditable;
}

// 指定の判定を持つスイッチ名の一覧(表の順序)。初回呼び出しで作ってキャッシュする。
inline const std::vector<std::string>& SystemSwitchNamesWithVerdict(
    SystemSwitchStaticVerdict verdict) {
    static const std::array<std::vector<std::string>, kSystemSwitchStaticVerdictCount> kCache = [] {
        std::array<std::vector<std::string>, kSystemSwitchStaticVerdictCount> built;
        for (const SystemSwitchAxis& axis : kSystemSwitchAxes)
            built[static_cast<size_t>(axis.staticVerdict)].push_back(axis.name);
        return built;
    }();
    return kCache[static_cast<size_t>(verdict)];
}

// Dynamic のうち、駅舎の実行時スイッチ(source == StationRuntime)を除いた名前の一覧(表の順序)。
// 駅舎の実行時スイッチが常に0の配置(信号プラグインのモデル)の静的判定に使う。
inline const std::vector<std::string>& SystemSwitchNamesDynamicExceptStationRuntime() {
    static const std::vector<std::string> kNames = [] {
        std::vector<std::string> built;
        for (const SystemSwitchAxis& axis : kSystemSwitchAxes) {
            if (axis.staticVerdict == SystemSwitchStaticVerdict::Dynamic &&
                axis.source != SystemSwitchSource::StationRuntime)
                built.push_back(axis.name);
        }
        return built;
    }();
    return kNames;
}

// 指定の出どころを持つスイッチ名の一覧(表の順序)。初回呼び出しで作ってキャッシュする。
inline const std::vector<const char*>& SystemSwitchNamesWithSource(SystemSwitchSource source) {
    constexpr size_t kSourceCount = static_cast<size_t>(SystemSwitchSource::None) + 1;
    static const std::array<std::vector<const char*>, kSourceCount> kCache = [] {
        std::array<std::vector<const char*>, kSourceCount> built;
        for (const SystemSwitchAxis& axis : kSystemSwitchAxes)
            built[static_cast<size_t>(axis.source)].push_back(axis.name);
        return built;
    }();
    return kCache[static_cast<size_t>(source)];
}

} // namespace rs::legacy_pi

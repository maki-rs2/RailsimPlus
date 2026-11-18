#pragma once

#include <cstddef>
#include <iterator>

#include <magic_enum/magic_enum.hpp>
#include "PluginHeader.h"


namespace rs::legacy_pi {

enum class PluginMeshMethod { Model, Extrude, Line };
enum class MultiTrackSource { None, Profile, Pier, Pole };
enum class ProfileSupport { None, Rail, Tie };

struct PluginTypeTraits {
    PluginType pluginType;
    const char* folderName;
    const char* defaultName;
    bool autoPlace;
    bool trackPlugin;
    bool edgeExtrusion;
    bool scenery;
    bool discreteRailPlacement;
    PluginMeshMethod meshMethod;
    MultiTrackSource multiTrackSource;
    ProfileSupport profileSupport;
    bool defaultCastShadow = true;
};

// 種別固有の性質は、読込・表示・差し替えの経路によらず同じ値を参照する。
inline constexpr PluginTypeTraits kPluginTypeTraits[] = {
    {PluginType::Unknown, "", "", false, false, false, false, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Train, "Train", "", false, false, false, false, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Rail, "Rail", "", false, true, true, false, false, PluginMeshMethod::Extrude, MultiTrackSource::None, ProfileSupport::Rail},
    {PluginType::Tie, "Tie", "", false, true, true, false, false, PluginMeshMethod::Extrude, MultiTrackSource::None, ProfileSupport::Tie},
    {PluginType::Girder, "Girder", "", false, true, true, false, false, PluginMeshMethod::Extrude, MultiTrackSource::Profile, ProfileSupport::None},
    {PluginType::Pier, "Pier", "", false, true, false, false, true, PluginMeshMethod::Model, MultiTrackSource::Pier, ProfileSupport::None},
    {PluginType::Pole, "Pole", "", false, true, false, false, false, PluginMeshMethod::Model, MultiTrackSource::Pole, ProfileSupport::None},
    {PluginType::Station, "Station", "", false, false, false, true, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Struct, "Struct", "", false, false, false, true, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Surface, "Surface", "Default", true, false, false, false, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None, false},
    {PluginType::Line, "Line", "", false, true, false, false, false, PluginMeshMethod::Line, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Env, "Env", "", false, false, false, false, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
    {PluginType::Skin, "Skin", "", false, false, false, false, false, PluginMeshMethod::Model, MultiTrackSource::None, ProfileSupport::None},
};

constexpr bool IsPluginTypeTraitsInEnumOrder() {
    for (std::size_t index = 0; index < std::size(kPluginTypeTraits); ++index) {
        if (static_cast<std::size_t>(kPluginTypeTraits[index].pluginType) != index) return false;
    }
    return true;
}

static_assert(std::size(kPluginTypeTraits) == magic_enum::enum_count<PluginType>());
static_assert(IsPluginTypeTraitsInEnumOrder());

constexpr const PluginTypeTraits& GetPluginTypeTraits(PluginType type) {
    const std::size_t index = static_cast<std::size_t>(type);
    return kPluginTypeTraits[index < std::size(kPluginTypeTraits) ? index : 0];
}

} // namespace rs::legacy_pi

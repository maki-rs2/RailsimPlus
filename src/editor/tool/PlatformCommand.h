#pragma once

#include <array>
#include <optional>

#include "Command.h"
#include "../../world/alignment/PlatformRegistry.h"

namespace rs::editor {

class AddStationCommand : public ICommand {
public:
    explicit AddStationCommand(rs::world::StationInfo station);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::StationInfo station_;
    size_t index_ = 0;
};

class RemoveStationCommand : public ICommand {
public:
    explicit RemoveStationCommand(rs::world::Guid stationId);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid stationId_;
    rs::world::StationInfo station_;
    size_t index_ = 0;
    bool removed_ = false;
    std::vector<rs::world::Guid> detachedPlatformIds_;
};

class AddPlatformCommand : public ICommand {
public:
    explicit AddPlatformCommand(rs::world::PlatformStopInfo platform);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::PlatformStopInfo platform_;
    size_t index_ = 0;
};

class RemovePlatformCommand : public ICommand {
public:
    explicit RemovePlatformCommand(rs::world::Guid platformId);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid platformId_;
    rs::world::PlatformStopInfo platform_;
    size_t index_ = 0;
    bool removed_ = false;
};

struct StationPropertyDelta { std::optional<std::string> name; };
struct PlatformPropertyDelta {
    std::optional<std::string> name;
    std::optional<rs::world::Guid> stationId;
    std::optional<bool> stoppable;
    std::optional<rs::world::StopReference> stopReference;
    std::optional<double> stopOffsetM;
    std::optional<std::vector<rs::world::PlatformRoutineElement>> routine;
    std::optional<std::vector<rs::world::PlatformTrainRoutine>> trainRoutines;
    std::optional<std::vector<rs::world::PlatformTagRoutine>> tagRoutines;
    std::optional<std::vector<rs::world::PlatformFormationRoutine>> formationRoutines;
    // [0]=左、[1]=右(PlatformStopInfo::openDoor と同じ並び)。
    std::optional<std::array<bool, 2>> openDoor;
    std::optional<std::vector<rs::world::Guid>> railEdges;
};

class SetStationPropertyCommand : public ICommand {
public:
    SetStationPropertyCommand(rs::world::Guid stationId, StationPropertyDelta delta);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid stationId_;
    StationPropertyDelta delta_;
    StationPropertyDelta oldValues_;
    bool hadOld_ = false;
};

class SetPlatformPropertyCommand : public ICommand {
public:
    SetPlatformPropertyCommand(rs::world::Guid platformId, PlatformPropertyDelta delta);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid platformId_;
    PlatformPropertyDelta delta_;
    PlatformPropertyDelta oldValues_;
    bool hadOld_ = false;
};

} // namespace rs::editor

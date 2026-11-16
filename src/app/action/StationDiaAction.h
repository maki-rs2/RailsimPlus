//
// StationDiaAction.h - 駅・ダイヤ編集のアプリサービス
//
#pragma once

#include <string>
#include <vector>

#include "../../editor/tool/Editor.h"

namespace rs::app {

struct StationDiaPlatformRowViewModel {
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
    bool selected = false;
};

struct StationDiaStationRowViewModel {
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
    bool selected = false;
    std::vector<StationDiaPlatformRowViewModel> platforms;
};

struct StationDiaSelectedStationViewModel {
    bool visible = false;
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
};

struct StationDiaSelectedPlatformViewModel {
    bool visible = false;
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
    bool stoppable = false;
    rs::world::StopReference stopReference = rs::world::StopReference::Center;
    double stopOffsetM = 0.0;
    rs::world::Guid stationId = rs::world::NilGuid();
    std::string stationName;
    int edgeCount = 0;
};

// ポイント一覧の1行。分岐している面ごとに、左右それぞれの行き先(links の添字)と開通状態を持つ。
struct StationDiaPointSideViewModel {
    int leftLinkIndex = 0;
    int rightLinkIndex = 0;
    bool leftActive = false;
    bool rightActive = false;
};

struct StationDiaPointRowViewModel {
    rs::world::Guid id = rs::world::NilGuid();
    float x = 0.0f;
    float z = 0.0f;
    bool selected = false;
    std::vector<StationDiaPointSideViewModel> sides;
};

struct StationDiaViewModel {
    std::vector<StationDiaStationRowViewModel> stations;
    // 位置(x,z)の小さい順。内部の並びは読込のたびに変わるので、表示用に固定する。
    std::vector<StationDiaPointRowViewModel> points;
    std::vector<StationDiaPlatformRowViewModel> unassignedPlatforms;
    StationDiaSelectedStationViewModel selectedStation;
    StationDiaSelectedPlatformViewModel selectedPlatform;
};

class StationDiaAction {
public:
    explicit StationDiaAction(rs::editor::Editor& editor);

    void AddStation();
    void AddPlatform();
    void SelectStation(rs::world::Guid stationId);
    void SelectPlatform(rs::world::Guid platformId);
    void BeginHoverFrame();
    void SetHoveredStation(rs::world::Guid stationId);
    void SetHoveredPlatform(rs::world::Guid platformId);
    void SelectPoint(rs::world::Guid nodeId);
    void SetHoveredPoint(rs::world::Guid nodeId);
    // nodeId のポイントで、links の添字 linkIndex の側を開通させる(Undo対象)。
    void SetPointActiveLink(rs::world::Guid nodeId, int linkIndex);
    void RenameStation(rs::world::Guid stationId, std::string name);
    void RemoveStation(rs::world::Guid stationId);
    void RenamePlatform(rs::world::Guid platformId, std::string name);
    // 所属駅を変える。NilGuidは未所属。
    void SetPlatformStation(rs::world::Guid platformId, rs::world::Guid stationId);
    // 進行方向のドア側を開けるか。side は 0=左、1=右(PlatformStopInfo::openDoor の添字)。
    void SetPlatformDoor(rs::world::Guid platformId, int side, bool open);
    void RenameSelectedPlatform(std::string name);
    void SetSelectedPlatformStoppable(bool stoppable);
    void SetSelectedPlatformStopReference(rs::world::StopReference reference);
    void SetSelectedPlatformStopOffset(double offsetM);
    void SetSelectedPlatformStation(rs::world::Guid stationId);
    void RemoveSelectedPlatform();
    StationDiaViewModel ViewModel() const;

private:
    rs::editor::Editor& editor_;
};

} // namespace rs::app

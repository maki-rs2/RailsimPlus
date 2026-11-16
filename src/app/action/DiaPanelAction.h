//
// DiaPanelAction.h - ダイヤモードの左タブ(RmlUi)の操作の入口
//
// 画面のイベントは表示値の添字で届く。各関数は、実行するときに表示値から番線のGuid・層のキー・
// 要素の添字へ引き直し、既存の StationDiaAction / PlatformRoutineAction を呼ぶ。
//
#pragma once

#include <functional>
#include <string>
#include <unordered_set>

#include "../../world/alignment/Guid.h"

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app {
class ToastQueue;

namespace workspace {
struct DiaPanelPresentation;
} // namespace workspace

struct DiaPanelActionRequest {
    int targetIndex_ = -1;
    int layerIndex_ = -1;
    int rowIndex_ = -1;
    int itemIndex_ = -1;
    int sourceIndex_ = -1;
    std::string field_;
    std::string value_;
};

// シーンの切り替えをまたぐ要求(別シーンの駅へカメラを動かす・別シーンの番線を開く)。
// 持ち主は左タブのビューで、切り替えが終わるまで要求を保持して毎フレーム進める。
struct DiaPanelNavigation {
    bool pending_ = false;
    int sceneIndex_ = 0;
    bool fly_ = false;
    float flyX_ = 0.0f;
    float flyY_ = 0.0f;
    float flyZ_ = 0.0f;
    bool selectPlatform_ = false;
    rs::world::Guid platformId_ = rs::world::NilGuid();
    // 別シーンの分岐を開くとき。シーンを切り替えたあとに Editor::selectedNode へ選ぶ。
    bool selectPoint_ = false;
    rs::world::Guid nodeId_ = rs::world::NilGuid();
    int waitedFrames_ = 0;
};

// 今、列車が在線している閉塞を返す。走行していなければ空。分岐の開通先の切り替えが使う。
using OccupiedBlocksSource = std::function<std::unordered_set<rs::world::Guid, rs::world::GuidHash>()>;

// 操作の対象を束ねた参照。どれも呼び出し側が所有する。
struct DiaPanelActionRefs {
    ToastQueue& queue;
    rs::editor::Editor& editor;
    const workspace::DiaPanelPresentation& panel;
    DiaPanelNavigation& navigation;
    const OccupiedBlocksSource& occupiedBlocksSource;

    std::unordered_set<rs::world::Guid, rs::world::GuidHash> OccupiedBlocks() const {
        return occupiedBlocksSource ? occupiedBlocksSource() : std::unordered_set<rs::world::Guid, rs::world::GuidHash>{};
    }
};

namespace dia_panel_action {
using Refs = DiaPanelActionRefs;
using Request = DiaPanelActionRequest;
void FlyToStation(const Refs& refs, const Request& request);
void RenameStation(const Refs& refs, const Request& request);
void DeleteStation(const Refs& refs, const Request& request);
void MovePlatform(const Refs& refs, const Request& request);
void ToggleDoor(const Refs& refs, const Request& request);
void ChangeOpening(const Refs& refs, const Request& request);
void AddLayer(const Refs& refs, const Request& request);
void DeleteLayer(const Refs& refs, const Request& request);
void ReorderLayer(const Refs& refs, const Request& request);
void AddElement(const Refs& refs, const Request& request);
void DeleteElement(const Refs& refs, const Request& request);
void DisableElement(const Refs& refs, const Request& request);
void ReorderElement(const Refs& refs, const Request& request);
void ChangeOperation(const Refs& refs, const Request& request);
void ChangeDepartureType(const Refs& refs, const Request& request);
void ChangeDepartureTime(const Refs& refs, const Request& request);
void AddDepartureTime(const Refs& refs, const Request& request);
void DeleteDepartureTime(const Refs& refs, const Request& request);
void ChangeConnectionWait(const Refs& refs, const Request& request);
void ResetStopPosition(const Refs& refs, const Request& request);
void ChangeStopPosition(const Refs& refs, const Request& request);
void ChangeRunInfo(const Refs& refs, const Request& request);
void AddRunInfo(const Refs& refs, const Request& request);
void DeleteRunInfo(const Refs& refs, const Request& request);
void EditCondition(const Refs& refs, const Request& request);
void ChangeBranchHand(const Refs& refs, const Request& request);
void AddCondition(const Refs& refs, const Request& request);
void DeleteCondition(const Refs& refs, const Request& request);
void DisableCondition(const Refs& refs, const Request& request);
void MovePoint(const Refs& refs, const Request& request);
} // namespace dia_panel_action
} // namespace rs::app

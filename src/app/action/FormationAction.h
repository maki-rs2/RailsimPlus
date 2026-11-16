//
// FormationAction.h - 編成パネルのアプリケーションサービス
//
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../sim/DiaRuntime.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class PluginSelector;
class SceneRenderer;
class CabinViewCamera;
class CameraController;
struct TrainPlacementUiState;
struct LayoutDocumentState;

struct FormationCarViewModel {
    std::string displayName;
    std::string folderUtf8;
    bool reversed = false;
};

struct TrainTagViewModel {
    int id = 0;
    std::string label;
};

struct FormationRowViewModel {
    std::string name;
    std::size_t carCount = 0;
    bool selected = false;
    // この編成の車両の並び(選択中でなくても入る)。
    std::vector<FormationCarViewModel> cars;
    // 編成の識別子。添字は削除・複製でずれるので、UIが対象を覚えておくときはこちらを使う。
    rs::world::Guid guid = rs::world::NilGuid();
    // この編成から置いた列車があるか。あるうちは同じ編成を置き直せない。
    bool placed = false;
    // この編成に付いているタグ(タグの定義の並び順)。
    std::vector<TrainTagViewModel> tags;
};

struct PlacedGroupViewModel {
    rs::world::TrainId trainId = rs::world::NilGuid();
    // 配置元の編成。NilGuidなら配置元が無く、タグは付けられない。
    rs::world::Guid sourceFormationId = rs::world::NilGuid();
    std::vector<bool> tagAssigned;
    float targetSpeedKmh = 0.0f;
    float currentSpeedKmh = 0.0f;
    rs::sim::DiaState diaState = rs::sim::DiaState::Running;
    bool hasDeparture = false;
    int departureSecondsOfDay = 0;
    // 運転台視点。この編成が現在の
    // カブ視点追従対象か(CabinViewCamera::active && followedTrainId==trainId)。
    bool cabinViewActive = false;
    // 運行情報(未設定は空文字)。編成情報カードの上段に出す。
    std::string trainType;
    std::string destination;
    std::string trainNumber;
    // 走行状態が属するSceneの添字。未走行ならnullopt。
    std::optional<std::size_t> sceneIndex;
};

struct FormationViewModel {
    std::vector<FormationRowViewModel> defs;
    bool hasSelectedDef = false;
    std::string selectedDefName;
    std::vector<FormationCarViewModel> selectedCars;
    bool placementReversedHead = false;
    std::vector<TrainTagViewModel> tags;
    std::vector<PlacedGroupViewModel> placedGroups;
    // どれかの編成で運転台視点が有効か。
    bool cabinViewActive = false;
};

// 運転台視点中の右上パネルに出す値。追従先が無い・視点がOFFのときはactiveがfalse。
struct CabinHudViewModel {
    bool active = false;
    // 運行情報(未設定は空文字)。
    std::string trainType;
    std::string destination;
    std::string previousStation;
    std::string nextStation;
    std::string nextPlatform;
    int speedKmh = 0;
    bool operator==(const CabinHudViewModel&) const = default;
};

struct FormationRefs {
    PluginSelector& selector;
    rs::world::SceneRegistry& sceneRegistry;
    SceneRenderer& sceneRenderer;
    TrainPlacementUiState& uiState;
    LayoutDocumentState& document;
    CabinViewCamera& cabinCamera;
    CameraController& camera;
};

class FormationAction {
public:
    explicit FormationAction(const FormationRefs& refs);

    void AddFormation();
    // 編成を選ぶ。置いた列車がある編成も選べる(置き直しの禁止は配置ツールが見る)。その場合は、列車の編成情報カードも広げる。
    void SelectFormation(std::size_t defIndex);
    void RemoveFormation(std::size_t defIndex);
    void SetSelectedFormationName(std::string name);
    // 選択を変えずに、defIndexの編成の名前を変える。範囲外は無視。
    void RenameFormation(std::size_t defIndex, std::string name);
    // 車両(並び・向き・スイッチ設定)と属性タグを写した編成を、元の直後に作って選択する。
    // 編成別の駅・ポイントのルーチンは、編成のguidをキーに持つので写らない。範囲外は無視。
    void DuplicateFormation(std::size_t defIndex);
    // guidの編成の現在の添字。無ければnullopt。
    std::optional<std::size_t> FindFormationIndex(const rs::world::Guid& formationId) const;
    // この編成から配置した列車の数。
    std::size_t PlacedTrainCount(const rs::world::Guid& formationId) const;
    bool ApplyTrainGroupTemplate(const std::string& path);

    // 車両の操作。defIndex付きは選択状態によらずその編成を対象にする。添字なしの版は選択中の編成に対する同じ操作。
    void SetCarReversed(std::size_t carIndex, bool reversed);
    void SetCarReversed(std::size_t defIndex, std::size_t carIndex, bool reversed);
    void MoveCar(std::size_t carIndex, int delta);
    void RemoveCar(std::size_t carIndex);
    void RemoveCar(std::size_t defIndex, std::size_t carIndex);
    // 車両carIndexを、スロットslot(`+`の位置。FormationCarSlots.h)へ動かす。並びが変わらない位置は何もしない。
    void MoveCarToSlot(std::size_t defIndex, std::size_t carIndex, std::size_t slot);
    // folderUtf8の列車プラグインを、スロットslotへ1両挿入する。失敗したら理由を返す(成功は空文字)。
    std::string InsertCar(std::size_t defIndex, std::size_t slot, const std::string& folderUtf8);

    void SetPlacementReversedHead(bool reversed);

    // 追加したタグのid。
    int AddTrainTag(const std::string& label);
    void RemoveTrainTag(int tagId);
    // タグの持ち主は編成の定義。同じ編成から置いた列車はすべて同じタグになる。
    void SetFormationTagAssigned(const rs::world::Guid& formationId, int tagId, bool assigned);

    void SetGroupTargetSpeedKmh(rs::world::TrainId trainId, float targetKmh);
    // 目標速度を、微調整ボタンのsteps回ぶん(符号つき)動かす。刻みと範囲は workspace::StepTargetSpeedKmh。
    void StepGroupTargetSpeedKmh(rs::world::TrainId trainId, int steps);
    void SetGroupStopDistance(rs::world::TrainId trainId, float distanceM);
    void ClearGroupStopTarget(rs::world::TrainId trainId);
    void ForceStopGroup(rs::world::TrainId trainId);
    void RemoveGroup(rs::world::TrainId trainId);

    // 運転台視点トグル。
    void SetCabinViewActive(rs::world::TrainId trainId, bool active);
    // 追従中の編成の運転台視点をやめる(「戻る」ボタンとEscの共通の経路)。追従先の編成は残す。
    void LeaveCabinView();
    CabinHudViewModel CabinHud() const;
    // 列車の先頭車へカメラの基準点を寄せ、そのまま毎フレーム先頭車の位置へ合わせ続ける(追跡)。運転台視点には入らず、
    // 向きと距離は変えない。WASDなど基準点を動かす操作でやめる。先頭車の位置が無ければ何もしない。
    void FollowTrain(rs::world::TrainId trainId);
    // 配置済みの編成(定義のguid)の先頭の列車を追跡する(列車一覧の配置済みカード)。
    void FollowFormation(const rs::world::Guid& formationId);
    // trainIdの編成の先頭車(走行方向込み)の車体の上面中央のワールド座標。先頭車が描画側に常駐していない
    // 間はfalse(編成情報カードを出さない)。
    bool ResolveLeadCarAnchor(rs::world::TrainId trainId, rs::core::Vec3& outAnchor) const;
    FormationViewModel ViewModel() const;

private:
    FormationRefs refs_;
};

} // namespace rs::app

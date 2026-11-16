//
// SceneCabinPivot.cpp - 運転台視点のカブピボット(FrontCabin/TailCabin)解決
// (SceneTrainController.h宣言、SceneTrainController.cpp/SceneTrainRenderSubmit.cppから
// 分離)。
//
#include "SceneTrainController.h"

#include <algorithm>
#include <cstring>

#include "../../plugins/schema/PluginSchema.h"

namespace rs::app {

namespace {

// def.partsの中から label と一致するパーツのワールド行列(partWorlds添字は
// defParts添字と同一)を返す。_LOCAL/_WORLD等のシステム名は恒等行列扱いする
// (rs::legacy_pi::resolveJoint3D/DynamicJointPlacementと同じ規約)。
bool ResolveJointFrame(const std::vector<rs::legacy_pi::ModelPart>& defParts,
                      const std::vector<rs::core::Mat4>& partWorlds, const std::string& label,
                      rs::core::Mat4& out) {
    if (rs::legacy_pi::IsSystemJointTargetName(label)) {
        out = rs::core::Mat4::Identity();
        return true;
    }
    for (size_t i = 0; i < defParts.size() && i < partWorlds.size(); ++i) {
        if (defParts[i].label == label) {
            out = partWorlds[i];
            return true;
        }
    }
    return false;
}

} // namespace

bool SelectCabinLeadCar(const std::vector<bool>& carReversedInOrder, bool travelReversed,
                        size_t& outLeadIndex, bool& outTailCabin) {
    if (carReversedInOrder.empty()) return false;
    outLeadIndex = travelReversed ? carReversedInOrder.size() - 1 : 0;
    outTailCabin = travelReversed != carReversedInOrder[outLeadIndex];
    return true;
}

bool ResolveCabinLeadCarEntityId(const rs::world::SceneRegistry& registry, TrainId trainId,
                                 bool travelReversed, rs::world::Guid& outLeadCarEntityId,
                                 bool& outTailCabin) {
    // 1. trainIdのCarエンティティを連結順(carOrder)で並べる。
    std::vector<const rs::world::SceneEntity*> cars;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Car) continue;
        if (entity.trainId != trainId) continue;
        cars.push_back(&entity);
    }
    if (cars.empty()) return false;
    std::sort(cars.begin(), cars.end(),
              [](const rs::world::SceneEntity* left, const rs::world::SceneEntity* right) {
                  return left->carOrder < right->carOrder;
              });

    // 2. 先頭/後尾どちらの車両・カブを使うか(純関数SelectCabinLeadCar、単体テスト対象)。
    std::vector<bool> carReversedInOrder;
    carReversedInOrder.reserve(cars.size());
    for (const rs::world::SceneEntity* car : cars) carReversedInOrder.push_back(car->reversed);
    size_t leadIndex = 0;
    if (!SelectCabinLeadCar(carReversedInOrder, travelReversed, leadIndex, outTailCabin)) return false;
    outLeadCarEntityId = cars[leadIndex]->guid;
    return true;
}

const SceneSlotPool::Slot* FindTrainCarSlot(const std::vector<SceneSlotPool::Slot>& slots,
                                            const rs::world::Guid& carEntityId) {
    for (const SceneSlotPool::Slot& candidate : slots) {
        if (candidate.used && candidate.isTrainCar && candidate.entityId == carEntityId) return &candidate;
    }
    return nullptr;
}

bool ResolveLeadCarAnchor(const rs::world::SceneRegistry& registry,
                          const std::vector<SceneSlotPool::Slot>& slots, TrainId trainId,
                          bool travelReversed, rs::core::Vec3& outAnchor) {
    rs::world::Guid leadCarEntityId = rs::world::NilGuid();
    bool tailCabin = false;
    if (!ResolveCabinLeadCarEntityId(registry, trainId, travelReversed, leadCarEntityId, tailCabin)) {
        return false;
    }
    const SceneSlotPool::Slot* slot = FindTrainCarSlot(slots, leadCarEntityId);
    if (slot == nullptr) return false;
    const SceneSlotPool::CarRenderSnapshot& car = slot->carSnapshot;
    if (!car.carPickValid || !car.carVisible) return false;

    // 箱は車体の行列(carPickFrame)のローカル座標。上面の中央をワールドへ置く(row-vector: v' = v * M)。
    const float localX = (car.carPickAabbMin[0] + car.carPickAabbMax[0]) * 0.5f;
    const float localY = car.carPickAabbMax[1];
    const float localZ = (car.carPickAabbMin[2] + car.carPickAabbMax[2]) * 0.5f;
    const float* frame = car.carPickFrame.m;
    outAnchor = rs::core::Vec3(localX * frame[0] + localY * frame[4] + localZ * frame[8] + frame[12],
                               localX * frame[1] + localY * frame[5] + localZ * frame[9] + frame[13],
                               localX * frame[2] + localY * frame[6] + localZ * frame[10] + frame[14]);
    return true;
}

bool SceneTrainController::ResolveCabinPivot(const rs::world::SceneRegistry& registry,
                                             const std::vector<SceneSlotPool::Slot>& slots,
                                             TrainId trainId, bool travelReversed,
                                             bool& outTailCabin,
                                             rs::core::Mat4& outPivotWorld) const {
    // 1〜2. 対象車両のSceneEntity::guidを解決する(共有ヘルパー、_CABINVIEWシステム
    // スイッチ付与(SceneTrainController::Tick)と同じ選定ロジック)。
    rs::world::Guid leadCarEntityId = rs::world::NilGuid();
    if (!ResolveCabinLeadCarEntityId(registry, trainId, travelReversed, leadCarEntityId,
                                     outTailCabin)) {
        return false;
    }

    // 3. 対応するGPU常駐スロットを探す(非常駐ならfalse)。
    const SceneSlotPool::Slot* slot = FindTrainCarSlot(slots, leadCarEntityId);
    if (slot == nullptr || !slot->carSnapshot.carSnapshotValid ||
        slot->carSnapshot.carDefPartWorlds.empty()) {
        return false;
    }

    // 4. 明示/フォールバックいずれかで確定済みのJoint3DRefを選ぶ
    //    (PluginSchema.cppのResolveCabinFallbackが読込時に補完済み)。
    const rs::legacy_pi::TrainInfo& trainInfo = slot->session.plugin.def.trainInfo;
    const rs::legacy_pi::Joint3DRef* joint =
        outTailCabin ? (trainInfo.hasTailCabin ? &trainInfo.tailCabin : nullptr)
                     : (trainInfo.hasFrontCabin ? &trainInfo.frontCabin : nullptr);
    if (joint == nullptr) return false;

    // 5. target/DirLink/UpLinkのラベルをdef.partsのワールド行列へ解決する。
    const std::vector<rs::legacy_pi::ModelPart>& defParts = slot->session.plugin.def.parts;
    const std::vector<rs::core::Mat4>& partWorlds = slot->carSnapshot.carDefPartWorlds;
    rs::core::Mat4 targetFrame{}, dirFrame{}, upFrame{};
    if (!ResolveJointFrame(defParts, partWorlds, joint->target, targetFrame) ||
        !ResolveJointFrame(defParts, partWorlds, joint->dirLink, dirFrame) ||
        !ResolveJointFrame(defParts, partWorlds, joint->upLink, upFrame)) {
        return false;
    }

    float outWorld[16];
    rs::legacy_pi::ComposeJoint3DWorld(*joint, dirFrame.m, upFrame.m, targetFrame.m, outWorld);
    std::memcpy(outPivotWorld.m, outWorld, sizeof(outWorld));
    return true;
}

} // namespace rs::app

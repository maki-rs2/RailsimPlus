//
// DynamicJointPlacement.cpp - ResolveDynamicPartWorlds の実装。
//
#include "DynamicJointPlacement.h"
#include "LabeledFrameResolver.h"
#include "../../schema/ZyLinkageSolve.h"

#include <cstring>

#include <spdlog/spdlog.h>

namespace rs::app {

namespace {

using rs::core::Mat4;
using rs::legacy_pi::ComposeJoint3DWorld;
using rs::legacy_pi::ModelPart;
using rs::legacy_pi::SolveZyGroupForPart;
using rs::legacy_pi::ZyFrameLookup;
using rs::legacy_pi::ZyGroupResult;

// name が指すパーツの今フレームのワールド行列を引く。resolveJoint3D の
// findFrameMatrix と同じ規約(_LOCAL/_WORLD=恒等、先頭一致優先、未解決なら
// false)だが、対象は resolved[]/world[] (実行時解決状態)。
bool FindDynamicFrame(const std::string& name, const std::vector<std::string>& labels,
                      const std::vector<bool>& resolved, const std::vector<Mat4>& world,
                      Mat4& outMat) {
    return FindLabeledFrame(name, labels, resolved, world, outMat);
}

} // namespace

} // namespace rs::app

namespace rs::legacy_plugin {

std::vector<rs::core::Mat4> UpdatePartMoverLocals(
    const rs::legacy_pi::PluginDefinition& def, std::vector<PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration, float dt,
    float dynamicRotateSpeedScale, double clockSecondsOfDay) {
    const std::vector<rs::legacy_pi::ModelPart>& parts = def.parts;
    std::vector<rs::core::Mat4> moverLocals(parts.size(), rs::core::Mat4::Identity());
    for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
        if (partIndex >= moverRuntimes.size() || moverRuntimes[partIndex].GroupCount() == 0) continue;
        // 未知の名前への参照で Joint3D 基準姿勢が
        // 恒等フォールバックになっているパーツは、StaticRotation/StaticMove を
        // 適用しない。本来の基準姿勢を前提に調整された回転量を、
        // 誤った基準へそのまま上乗せすると見た目が大きくおかしくなるため
        // （例: 車体が横倒しに見える。resolveJoint3D の hasUnresolvedJointRef 参照）。
        if (parts[partIndex].hasUnresolvedJointRef) continue;
        PartMoverRuntime& mover = moverRuntimes[partIndex];
        mover.UpdateActivation(resolve, switchGeneration);
        if (!mover.Settled()) mover.Update(dt, dynamicRotateSpeedScale, clockSecondsOfDay);
        float local[16];
        mover.ComposeLocal(local);
        std::memcpy(moverLocals[partIndex].m, local, sizeof(local));
    }
    return moverLocals;
}

} // namespace rs::legacy_plugin

namespace rs::app {

std::vector<Mat4> ResolveDynamicPartWorlds(
    const rs::legacy_pi::PluginDefinition& def,
    std::vector<rs::legacy_plugin::PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration, float dt,
    float dynamicRotateSpeedScale, double clockSecondsOfDay) {
    const std::vector<ModelPart>& parts = def.parts;
    size_t n = parts.size();
    std::vector<std::string> labels;
    labels.reserve(n);
    for (const ModelPart& part : parts) labels.push_back(part.label);

    // 1. 全パーツの自身のmover(StaticMove/StaticRotation/DynamicRotation)を
    //    1回だけ更新し、パーツローカルのアニメ行列を得る。表示中かどうかに
    //    関わらず全パーツ対象(非表示アンカーの mover も他パーツへ伝播する
    //    必要があるため)。
    const std::vector<Mat4> animLocal = rs::legacy_plugin::UpdatePartMoverLocals(
        def, moverRuntimes, resolve, switchGeneration, dt, dynamicRotateSpeedScale, clockSecondsOfDay);
    // 2. Joint3Dを持たないパーツは、パース時に確定済みの静的offset行列
    //    (part.worldMat、スイッチに依存しない)へ自身のmoverを重ねるだけで
    //    今フレームのワールド行列が決まる。
    std::vector<Mat4> world(n, Mat4::Identity());
    std::vector<bool> resolved(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (!parts[i].joints3d.empty() || parts[i].isZyPart) continue;
        Mat4 base{};
        std::memcpy(base.m, parts[i].worldMat, sizeof(base.m));
        world[i] = Mat4::Mul(animLocal[i], base);
        resolved[i] = true;
    }

    // 3. Joint3Dを持つパーツを、resolveJoint3D と同じ反復パス構造で解決する。
    //    基準フレーム(dir/up/target)には「参照先パーツの今フレームのワールド
    //    行列」(=そのパーツ自身のmover・さらにその親のJoint3D階層も込み)を
    //    渡すことで、mover の変位がJoint3Dチェーンを通じて子へ伝播する。
    for (size_t pass = 0; pass < n + 1; ++pass) {
        bool progressed = false;
        for (size_t i = 0; i < n; ++i) {
            if (resolved[i]) continue;
            if (parts[i].isZyPart) {
                // ZY系は組(自分と相手)をまとめて1回で解く(resolveJoint3D と同じ
                // SolveZyGroupForPart)。相手が見つからない、または取り付け先が
                // 未解決ならこのパスは据え置く。解いた行列に各自の mover を掛ける。
                ZyGroupResult group;
                const ZyFrameLookup lookup = [&](const std::string& name, Mat4& outFrame) {
                    return FindDynamicFrame(name, labels, resolved, world, outFrame);
                };
                if (!SolveZyGroupForPart(parts, i, lookup, group)) continue;
                for (int k = 0; k < group.count; ++k) {
                    const size_t memberIdx = group.indices[k];
                    Mat4 solved{};
                    std::memcpy(solved.m, group.worlds[k], sizeof(solved.m));
                    world[memberIdx] = Mat4::Mul(animLocal[memberIdx], solved);
                    resolved[memberIdx] = true;
                }
                progressed = true;
                continue;
            }
            if (parts[i].joints3d.empty()) continue; // 2.で解決済みのはず
            // resolveJoint3D と同じく、複数Joint3Dがある場合は最後の1つを採用。
            const rs::legacy_pi::Joint3DRef& j = parts[i].joints3d.back();
            Mat4 targetFrame{}, dirFrame{}, upFrame{};
            bool ok = FindDynamicFrame(j.target, labels, resolved, world, targetFrame);
            ok = ok && FindDynamicFrame(j.dirLink, labels, resolved, world, dirFrame);
            ok = ok && FindDynamicFrame(j.upLink, labels, resolved, world, upFrame);
            if (!ok) continue; // 依存先が未解決。次のパスへ持ち越し

            float jointWorld[16];
            ComposeJoint3DWorld(j, dirFrame.m, upFrame.m, targetFrame.m, jointWorld);
            Mat4 jointMat{};
            std::memcpy(jointMat.m, jointWorld, sizeof(jointMat.m));
            world[i] = Mat4::Mul(animLocal[i], jointMat);
            resolved[i] = true;
            progressed = true;
        }
        if (!progressed) break;
    }

    // 循環参照等で解決できなかったパーツは恒等行列のまま確定する
    // (パース時の resolveJoint3D で既に警告済みのため、ここでは静かに続行)。
    for (size_t i = 0; i < n; ++i)
        if (!resolved[i]) world[i] = Mat4::Identity();

    return world;
}

std::vector<Mat4> ResolveSettledPartWorlds(
    const rs::legacy_pi::PluginDefinition& def,
    std::vector<rs::legacy_plugin::PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration) {
    // 1ステップで進める時間。Pre/ReverseDelayと位相の所要時間(AnimationTime等)を
    // 1回のUpdateで消化し切れる大きさにする(位相は目標値で打ち止めになる)。
    constexpr float kSettleStepSeconds = 1.0e6f;
    constexpr int kMaxSettleSteps = 8;

    // 前の呼び出しの位相を引き継がないよう、毎回Build直後(Alwaysのみ適用済み)へ戻す。
    const size_t partCount = def.parts.size();
    for (size_t i = 0; i < partCount && i < moverRuntimes.size(); ++i)
        moverRuntimes[i].Build(def.parts[i]);

    std::vector<Mat4> world;
    for (int step = 0; step < kMaxSettleSteps; ++step) {
        world = ResolveDynamicPartWorlds(def, moverRuntimes, resolve, switchGeneration,
                                         kSettleStepSeconds, 0.0f, 0.0);
        bool allSettled = true;
        for (size_t i = 0; i < partCount && i < moverRuntimes.size(); ++i) {
            // ResolveDynamicPartWorldsが更新しないパーツ(未解決Joint参照)と、静止しない
            // 時間駆動の可動部(初期姿勢のまま使う)を持つパーツは見ない。
            if (def.parts[i].hasUnresolvedJointRef || moverRuntimes[i].HasTimeDrivenItems())
                continue;
            if (!moverRuntimes[i].Settled()) allSettled = false;
        }
        if (allSettled) return world;
    }
    spdlog::warn("[staticbake] 可動部の位相が{}回の更新で静止しませんでした。途中の状態で焼きます",
                 kMaxSettleSteps);
    return world;
}

} // namespace rs::app

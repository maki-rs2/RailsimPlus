//
// StaticMeshVariantBake.cpp
//
#include "StaticMeshVariantBake.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>

#include "../../plugins/load/ModelMeshHelpers.h"
#include "../../plugins/load/runtime/DynamicJointPlacement.h"
#include "../../plugins/load/runtime/StretchZyRuntime.h"
#include "../plugin_session/SharedStateAxes.h"

namespace rs::app {

namespace {

std::vector<rs::core::Mat4> CollectSessionPartWorlds(
    PluginSession& session, std::vector<rs::core::Mat4>& dynamicWorlds) {
    if (session.movablePartCount == 0)
        return rs::legacy_plugin::CollectStaticPartWorlds(session.plugin);
    // Move/Rotateの可動部は、スロット経路と同じ解決(ResolveDynamicPartWorlds)で位相を
    // 目標まで進め切った姿勢にする。
    const rs::legacy_pi::SwitchResolver resolve = [&session](const std::string& name) {
        return session.ResolveSwitch(name);
    };
    dynamicWorlds = ResolveSettledPartWorlds(
        session.plugin.def, session.moverRuntimes, resolve, session.SwitchGeneration());
    return rs::legacy_plugin::GatherAssemblyPartWorlds(session.plugin, dynamicWorlds);
}

// 現在のスイッチ状態を焼き込み、パートワールドとAABBまで済ませたメッシュを返す。
rs::render::Mesh BakeOne(PluginSession& session, const std::vector<int32_t>& switchOptions,
                         std::optional<int32_t> camDistBand,
                         std::optional<rs::world::SceneSharedSwitchValues> sharedState) {
    // .rs2レイアウト等が持つ配置時点のスイッチ確定値を焼き込む。全スイッチ既定0のままだと
    // ChangeTexture/ChangeModelがCase未選択のまま.x素のテンプレート参照を使い続け、
    // プレースホルダ表示になる。
    session.ApplySwitchOptionsAndBake(switchOptions, camDistBand, sharedState);
    return BakeSessionAssemblyMesh(session);
}

// 焼き上がりが同じメッシュがvariantsにあればその添字を、無ければ末尾へ足した添字を返す。
uint32_t AddOrFindVariant(StaticMeshVariantSet& set, rs::render::Mesh&& mesh) {
    for (size_t i = 0; i < set.variants.size(); ++i) {
        if (MeshesIdentical(set.variants[i], mesh)) return static_cast<uint32_t>(i);
    }
    set.variants.push_back(std::move(mesh));
    return static_cast<uint32_t>(set.variants.size() - 1);
}

// 現在のスイッチ状態で点灯条件が真の灯具と、そのときの部品行列(def.parts と同じ並び)。
// 可動部がなければ定義の行列、あれば静的な焼き込みと同じ「位相を目標まで進め切った姿勢」。
std::shared_ptr<const std::vector<StaticHeadlight>> CollectActiveHeadlights(PluginSession& session) {
    if (session.headlightAttachments.empty()) return nullptr;
    std::vector<rs::core::Mat4> partWorlds(session.plugin.def.parts.size(), rs::core::Mat4::Identity());
    const rs::legacy_pi::SwitchResolver resolve = [&session](const std::string& name) -> int32_t {
        return session.ResolveSwitch(name);
    };
    if (session.movablePartCount > 0) {
        partWorlds = ResolveSettledPartWorlds(session.plugin.def, session.moverRuntimes, resolve,
                                              session.SwitchGeneration());
    } else {
        for (size_t i = 0; i < partWorlds.size(); ++i)
            std::memcpy(partWorlds[i].m, session.plugin.def.parts[i].worldMat, sizeof(partWorlds[i].m));
    }
    std::vector<StaticHeadlight> lights;
    for (const HeadlightAttachment& attachment : session.headlightAttachments) {
        if (attachment.partDefIndex < 0 || static_cast<size_t>(attachment.partDefIndex) >= partWorlds.size())
            continue;
        if (!rs::legacy_pi::IsTriggerActive(attachment.trig, resolve)) continue;
        lights.push_back({attachment, partWorlds[static_cast<size_t>(attachment.partDefIndex)]});
    }
    if (lights.empty()) return nullptr;
    return std::make_shared<const std::vector<StaticHeadlight>>(std::move(lights));
}

// 現在のスイッチ状態で発動条件が真のSetAnimationがあるか。静的バッチは時間で材質を更新できない。
bool AnySetAnimationActive(const PluginSession& session) {
    const rs::legacy_pi::SwitchResolver resolve = [&session](const std::string& name) -> int32_t {
        return session.ResolveSwitch(name);
    };
    bool active = false;
    for (const rs::legacy_pi::ModelPart& part : session.plugin.def.parts) {
        part.ForEachSetAnimation([&](const rs::legacy_pi::SetAnimationCustomizer& animation) {
            active = active || rs::legacy_pi::IsTriggerActive(animation.trig, resolve);
        });
    }
    return active;
}

} // namespace

// BuildAssemblyMeshはパーツのオフセット(Joint3D/JointZY)を焼き込まず実行時に
// gPartWorldで与える前提だが、GPUインスタンス化パスは実行時のパートワールド更新を
// 経由しないため、ここで1回だけ焼き込む(漏れると静的アセンブリの回転/位置がずれる)。
rs::render::Mesh BakeSessionAssemblyMesh(PluginSession& session) {
    rs::render::Mesh mesh = session.plugin.assemblyMesh;
    std::vector<rs::core::Mat4> dynamicWorlds;
    const std::vector<rs::core::Mat4> partWorlds = CollectSessionPartWorlds(session, dynamicWorlds);
    if (session.movablePartCount > 0) {
        const std::vector<rs::render::MeshVertex> vertices =
            rs::legacy_plugin::BuildStretchZyDynamicVertices(session.plugin, dynamicWorlds);
        std::copy(vertices.begin(), vertices.end(), mesh.vertices.end() - static_cast<std::ptrdiff_t>(vertices.size()));
    }
    rs::legacy_plugin::BakeStaticPartWorlds(mesh, partWorlds);
    // モデル実寸バウンディング(ローカル空間)。Scenery選択ピッキングの共通基盤。
    mesh.ComputeAabb();
    return mesh;
}

bool BakeStaticMeshVariants(PluginSession& session, const std::vector<int32_t>& switchOptions,
                            StaticMeshVariantSet& out, bool stationRuntimeConstant) {
    out = StaticMeshVariantSet();
    // 今の hasAssembly は前回の焼き込み(空のモデルを選んだバンド等)で false のまま残りうるので、
    // セッションを使い回しても毎回読み直した場合と同じ判定になるよう読込直後の値を見る。
    if (!session.plugin.ok || !session.plugin.hasAssemblyAtLoad) return false;
    const StaticEligibility eligibility =
        ComputeStaticEligibility(session.plugin.def, session.hasTimeDrivenMovers,
                                 stationRuntimeConstant);
    if (!eligibility.IsBakeableExceptAnimation()) return false;

    const bool referencesCamDist = session.ReferencesCamDist();
    // 定義上は参照するのに登録が無い食い違いは対象外にする。
    if (!referencesCamDist && eligibility.referencesCamDistBand) return false;
    if (referencesCamDist && !session.CamDistQuantizationSafe()) return false;
    const uint32_t sharedStateMask = SharedStateAxisMask(session.SystemSwitches());
    if (sharedStateMask == 0 && eligibility.referencesSharedStateAxis) return false;

    if (referencesCamDist) out.bandStarts = session.CamDistBandStarts();
    out.sharedStateMask = sharedStateMask;
    const size_t stateCount = SharedStateSlotCount(sharedStateMask);
    const size_t bandCount = out.BandCount();
    out.variantOfBand.reserve(stateCount * bandCount);
    for (size_t state = 0; state < stateCount; ++state) {
        std::optional<rs::world::SceneSharedSwitchValues> sharedState;
        if (sharedStateMask != 0) sharedState = SharedStateSlotValues(sharedStateMask, state);
        for (size_t band = 0; band < bandCount; ++band) {
            std::optional<int32_t> bandStart;
            if (referencesCamDist) bandStart = out.bandStarts[band];
            rs::render::Mesh mesh = BakeOne(session, switchOptions, bandStart, sharedState);
            // 実行中に変わるのはバンドと共有状態だけなので、全部の組で動かなければ、このスイッチ値の
            // 配置でアニメは動かない。スイッチ値が変わると別の組として判定し直す。
            if (AnySetAnimationActive(session)) {
                out = StaticMeshVariantSet();
                return false;
            }
            out.variantOfBand.push_back(AddOrFindVariant(out, std::move(mesh)));
            // 灯具の点灯条件は、いま焼いた組の状態で判定する(_NIGHTなどの値が組ごとに違うため)。
            out.headlightsOfVariant.push_back(CollectActiveHeadlights(session));
        }
    }
    return true;
}

} // namespace rs::app

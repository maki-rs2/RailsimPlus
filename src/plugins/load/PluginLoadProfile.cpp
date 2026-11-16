//
// PluginLoadProfile.cpp - LoadPluginFolder() の計測スキャフォールド
// (LoadProfile::RecordVariant / LogLoadProfile)。環境変数 RS_PROFILE_LOAD
// 指定時のみログを出す計測専用コード。本体ロジックは PluginLoader.cpp /
// PluginPartModels.cpp を参照。
//
#include "PluginLoadInternal.h"

#include "../../platform/EnvironmentVariable.h"
#include <cstdio>
#include <cstdlib>

#include "../../render/resource/ModelManager.h"

namespace rs::legacy_plugin {

void LoadProfile::RecordVariant(const rs::legacy_pi::ModelPart& part,
                                const PartDynamicModel& dyn) {
    variantCount += static_cast<int>(part.changeModels.size()) + 1;
    variantPaths.insert(part.baseModelFile);
    std::set<std::string> perPart;
    perPart.insert(part.baseModelFile);
    for (const auto& cm : part.changeModels) {
        variantPaths.insert(cm.modelFile);
        perPart.insert(cm.modelFile);
    }
    variantDedupable +=
        static_cast<int>(part.changeModels.size()) + 1 - static_cast<int>(perPart.size());

    // [profile] 全候補常駐時の上限見積り用。パスごと(重複排除済み)に
    // 頂点数/インデックス数を記録する。
    auto recordVariantStats = [&](const std::string& path, const ModelVariant& v) {
        if (path.empty() || !v.ok) return;
        if (variantPathStats.find(path) != variantPathStats.end()) return;
        variantPathStats[path] = {v.meshUnscaled.vertices.size(),
                                  v.meshUnscaled.indices.size()};
    };
    if (!dyn.variants) return;
    recordVariantStats(part.baseModelFile, dyn.variants->baseVariant);
    for (size_t ci = 0; ci < part.changeModels.size(); ++ci)
        recordVariantStats(part.changeModels[ci].modelFile, dyn.variants->changeModelVariants[ci]);
}

// [profile] フェーズ別内訳を出力(計測専用。環境変数 RS_PROFILE_LOAD 指定時のみ)。
void LogLoadProfile(LoadedPlugin& lp, const LoadProfile& prof, double totalMs) {
    if (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr) {
        const double accounted = prof.parseMs + prof.assimpMs + prof.pbrMs + prof.variantMs +
                                 prof.animPathMs + prof.assemblyMs + prof.appendMs;

        // [profile] 実使用中のassemblyMeshと、ChangeModel全候補を重複排除して
        // 合計した場合(=全候補を常駐させたときの上限見積り)の頂点/インデックス数。
        std::size_t supersetVerts = 0;
        std::size_t supersetIdx = 0;
        for (const auto& kv : prof.variantPathStats) {
            supersetVerts += kv.second.first;
            supersetIdx += kv.second.second;
        }
        const std::size_t assemblyVerts = lp.assemblyMesh.vertices.size();
        const std::size_t assemblyIdx = lp.assemblyMesh.indices.size();
        const double ratio = assemblyVerts > 0
                                  ? static_cast<double>(supersetVerts) / static_cast<double>(assemblyVerts)
                                  : 0.0;

        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "[profile] total=%.1fms parse=%.1fms assimp=%.1fms(x%d) "
                      "applyPbr=%.1fms variant=%.1fms(x%d,uniq=%zu,dedupable=%d"
                      ",x=%.1fms,pbr=%.1fms) animPath=%.1fms "
                      "assembly=%.1fms append=%.1fms other=%.1fms "
                      "assemblyVerts=%zu/idx=%zu supersetVerts=%zu/idx=%zu ratio=%.1fx",
                      totalMs, prof.parseMs, prof.assimpMs, prof.assimpCount, prof.pbrMs,
                      prof.variantMs, prof.variantCount, prof.variantPaths.size(),
                      prof.variantDedupable, prof.variantXMs, prof.variantPbrMs,
                      prof.animPathMs, prof.assemblyMs, prof.appendMs, totalMs - accounted,
                      assemblyVerts, assemblyIdx, supersetVerts, supersetIdx, ratio);
        lp.log.push_back(buf);

        // .xメッシュキャッシュのヒット(コピーのみ)/ミス(パース+挿入)の内訳。
        const rs::render::ModelManager::Stats st = rs::render::GetModelManager().GetStats();
        std::snprintf(buf, sizeof(buf),
                      "[profile] xcache hits=%d %.1fms misses=%d %.1fms resident=%.1fMB/%zuMB",
                      st.hits, st.hitMs, st.misses, st.missMs,
                      static_cast<double>(rs::render::GetModelManager().Bytes()) / (1024.0 * 1024.0),
                      rs::render::ModelManager::BudgetBytes() / (size_t{1024} * 1024));
        lp.log.push_back(buf);
    }
}

} // namespace rs::legacy_plugin

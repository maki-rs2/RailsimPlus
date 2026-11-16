#include "ParsedPluginCache.h"
//
// StaticMeshCache.cpp
//
#include "StaticMeshCache.h"

#include "../../platform/EnvironmentVariable.h"
#include <chrono>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "StaticMeshVariantBake.h"
#include "../../plugins/load/ModelMeshHelpers.h"
#include "../../plugins/load/PluginPaths.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {

bool StaticMeshCache::RefreshGeneration() {
    const uint64_t generation = Generation();
    if (generation_ == generation) return false;
    staticClassCache_.clear();
    staticClassLru_.clear();
    retainedProbeIndex_.clear();
    retainedProbeSessions_.clear();
    trackPluginAssemblyCache_.clear();
    trackPluginLru_.clear();
    pierAssemblyCache_.clear();
    pierAssemblyLru_.clear();
    switchDefsCache_.clear();
    stationSidecarCache_.clear();
    trainSidecarCache_.clear();
    generation_ = generation;
    return true;
}

StaticMeshCache::StaticClassification StaticMeshCache::ClassifyStaticSession(
    PluginSession& session, const std::vector<int32_t>& switchOptions,
    bool stationRuntimeConstant) {
    StaticClassification cls;
    // 判定・スイッチ確定値の焼き込み・パートワールド焼き込み・AABB計算は
    // _CAMDISTのバンドと共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)の状態スロットごとに行う
    // (StaticMeshVariantBake.cpp参照)。
    cls.eligible = BakeStaticMeshVariants(session, switchOptions, cls.variants, stationRuntimeConstant);
    return cls;
}

void StaticMeshCache::RetainProbeSession(const std::string& folderUtf8, PluginSession&& session) {
    std::unordered_map<std::string,
                       std::list<std::pair<std::string, PluginSession>>::iterator>::iterator idx =
        retainedProbeIndex_.find(folderUtf8);
    if (idx != retainedProbeIndex_.end()) {
        // 既存エントリを最新のsessionへ置き換え、最近使用順の先頭へ付け替える。
        idx->second->second = std::move(session);
        retainedProbeSessions_.splice(retainedProbeSessions_.begin(), retainedProbeSessions_,
                                      idx->second);
        return;
    }
    retainedProbeSessions_.emplace_front(folderUtf8, std::move(session));
    retainedProbeIndex_[folderUtf8] = retainedProbeSessions_.begin();
    while (retainedProbeSessions_.size() > kMaxRetainedProbeSessions) {
        const std::pair<std::string, PluginSession>& oldest = retainedProbeSessions_.back();
        retainedProbeIndex_.erase(oldest.first);
        retainedProbeSessions_.pop_back();
    }
}

PluginSession* StaticMeshCache::FindRetainedProbeSession(const std::string& folderUtf8) {
    std::unordered_map<std::string,
                       std::list<std::pair<std::string, PluginSession>>::iterator>::iterator idx =
        retainedProbeIndex_.find(folderUtf8);
    if (idx == retainedProbeIndex_.end()) return nullptr;
    retainedProbeSessions_.splice(retainedProbeSessions_.begin(), retainedProbeSessions_,
                                  idx->second);
    return &idx->second->second;
}

bool StaticMeshCache::TryGetStaticInstanceMesh(const std::string& folderUtf8,
                                               const std::vector<int32_t>& switchOptions,
                                               std::vector<std::string>& log,
                                               const rs::render::Mesh** outMesh, bool signalModel, const std::string& definitionFile) {
    const StaticMeshVariantSet* variants = nullptr;
    if (!TryGetStaticInstanceVariants(folderUtf8, switchOptions, log, &variants, signalModel, definitionFile) ||
        variants->variants.empty()) {
        *outMesh = nullptr;
        return false;
    }
    // バリアントが複数あるとき(_CAMDISTか共有状態を参照)は、最初の空でない
    // バリアントを返す(ピッキングとハイライトの用途)。全部空ならvariants[0]。
    for (const rs::render::Mesh& mesh : variants->variants) {
        if (!mesh.Empty()) {
            *outMesh = &mesh;
            return true;
        }
    }
    *outMesh = &variants->variants[0];
    return true;
}

bool StaticMeshCache::TryGetStaticInstanceVariants(const std::string& folderUtf8,
                                                   const std::vector<int32_t>& switchOptions,
                                                   std::vector<std::string>& log,
                                                   const StaticMeshVariantSet** outVariants, bool signalModel, const std::string& definitionFile) {
    RefreshGeneration();
    const std::string key = MakePluginMeshKey(MakeStaticInstanceKey(folderUtf8, switchOptions, signalModel, definitionFile));
    // 信号のモデルには列車の接近も停車も無いので、駅舎の実行時スイッチを常に0とみなす。
    const bool stationRuntimeConstant = signalModel;
    std::unordered_map<std::string, StaticClassification>::iterator it = staticClassCache_.find(key);
    if (it == staticClassCache_.end()) {
        StaticClassification cls;
        std::vector<std::string> probeLog;
        PluginSession* retained = FindRetainedProbeSession(MakePluginSourceKey(folderUtf8, definitionFile));
        if (retained != nullptr) {
            // 配置スイッチ編集UI(TryGetSwitchDefs)が既にLoadFolder済みのsessionを
            // 保持している場合、ここでのディスク再読込を省く
            // (スイッチ切替のヒッチ対策)。
            cls = ClassifyStaticSession(*retained, switchOptions, stationRuntimeConstant);
        } else {
            // [switchprof] 計測専用: このLoadFolderは
            // SceneSlotSynchronizerのswitchProf.loadMsとは別経路(probe専用の使い捨て
            // PluginSession)のため、[switchprof]の内訳に載らずpass1所要時間へ
            // 埋もれる。累積時間・回数を
            // probeLoadMs_/probeLoadCount_へ貯め、SceneSlotSynchronizer::Syncが
            // TakeProbeLoadMs/TakeProbeLoadCountで取り出して[switchprof]へ合流させる。
            static const bool kProfileStaticProbe = rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr;
            const std::chrono::steady_clock::time_point tProbe0 = std::chrono::steady_clock::now();
            PluginSession probe;
            const bool cacheHit = ParsedPlugins().LoadInto(probe, PluginSource{folderUtf8, definitionFile}, probeLog);
            if (kProfileStaticProbe && !cacheHit) {
                probeLoadMs_ += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - tProbe0)
                                    .count();
                ++probeLoadCount_;
            }
            if (kProfileStaticProbe && cacheHit) ++probeCacheHitCount_;
            cls = ClassifyStaticSession(probe, switchOptions, stationRuntimeConstant);
            // 同じフォルダをスイッチ値違いで多数配置するレイアウトでは、組ごとに読込結果を
            // 複製し直すのが読込時間の大半を占める(定義の大きい情景で1組あたり約20ms)。
            // 次の組では上の保持セッション経路で使い回す。
            RetainProbeSession(MakePluginSourceKey(folderUtf8, definitionFile), std::move(probe));
        }
        it = staticClassCache_.emplace(key, std::move(cls)).first;
        it->second.lruIt = staticClassLru_.insert(staticClassLru_.begin(), key);
        if (!it->second.eligible)
            log.insert(log.end(), probeLog.begin(), probeLog.end());
        // ここでは破棄しない(PruneStaticMeshCache のコメント参照)。1フレーム内の
        // リビルドで上限を一時的に超えることは許容し、破棄は安全な地点でのみ行う。
    } else {
        staticClassLru_.splice(staticClassLru_.begin(), staticClassLru_, it->second.lruIt);
    }
    if (!it->second.eligible) {
        *outVariants = nullptr;
        return false;
    }
    *outVariants = &it->second.variants;
    return true;
}

void StaticMeshCache::PruneStaticMeshCache(const std::unordered_set<std::string>& liveKeys) {
    // LRU末尾(最も長く使われていない側)から、live以外を破棄する。liveだけが
    // 残った時点で上限未満にできなくても打ち切る(理由はヘッダのコメント参照)。
    std::list<std::string>::iterator it = staticClassLru_.end();
    while (staticClassCache_.size() > kMaxStaticClassEntries && it != staticClassLru_.begin()) {
        --it;
        if (liveKeys.count(*it) != 0) continue;
        staticClassCache_.erase(*it);
        it = staticClassLru_.erase(it);
    }
}

void StaticMeshCache::PruneTrackPluginAssemblyCache(const std::unordered_set<std::string>& liveKeys) {
    // LRU末尾(最も長く使われていない側)から、live以外を破棄する。liveだけが
    // 残った時点で上限未満にできなくても打ち切る(理由はヘッダのコメント参照)。
    std::list<std::string>::iterator it = trackPluginLru_.end();
    while (trackPluginAssemblyCache_.size() > kMaxTrackPluginAssemblyEntries &&
           it != trackPluginLru_.begin()) {
        --it;
        if (liveKeys.count(*it) != 0) continue;
        trackPluginAssemblyCache_.erase(*it);
        it = trackPluginLru_.erase(it);
    }
}

void StaticMeshCache::PrunePierAssemblyCache(const std::unordered_set<std::string>& liveKeys) {
    // LRU末尾(最も長く使われていない側)から、live以外を破棄する。liveだけが
    // 残った時点で上限未満にできなくても打ち切る(理由はヘッダのコメント参照)。
    std::list<std::string>::iterator it = pierAssemblyLru_.end();
    while (pierAssemblyCache_.size() > kMaxPierAssemblyEntries && it != pierAssemblyLru_.begin()) {
        --it;
        if (liveKeys.count(*it) != 0) continue;
        pierAssemblyCache_.erase(*it);
        it = pierAssemblyLru_.erase(it);
    }
}

bool StaticMeshCache::TryGetTrackPluginAssemblyMesh(const std::string& folderUtf8,
                                                   std::vector<std::string>& log,
                                                   const rs::render::Mesh** outMesh) {
    RefreshGeneration();
    const std::string key = MakePluginMeshKey(MakePluginSourceKey(folderUtf8, ""));
    std::unordered_map<std::string, TrackPluginClassification>::iterator it = trackPluginAssemblyCache_.find(key);
    if (it == trackPluginAssemblyCache_.end()) {
        PluginSession probe;
        std::vector<std::string> probeLog;
        rs::app::GetParsedPluginCache().LoadInto(probe, folderUtf8, probeLog);
        TrackPluginClassification cls;
        cls.ok = probe.plugin.ok && probe.plugin.hasAssembly;
        if (cls.ok) {
            // 焼き込み(パーツのオフセット)とモデル実寸バウンディングの計算を1回だけ行い
            // キャッシュに持つ(配置ピッキングのモデル実寸バウンディング用)。
            // EditorViewport.cppのPickTrackPluginPlacementが
            // mesh->aabbMin/aabbMaxをそのまま読む。
            cls.mesh = BakeSessionAssemblyMesh(probe);
        }
        it = trackPluginAssemblyCache_.emplace(key, std::move(cls)).first;
        it->second.lruIt = trackPluginLru_.insert(trackPluginLru_.begin(), key);
        if (!it->second.ok) log.insert(log.end(), probeLog.begin(), probeLog.end());
        // ここでは破棄しない(PruneStaticMeshCache/PruneTrackPluginAssemblyCache の
        // コメント参照)。1フレーム内のリビルドで上限を一時的に超えることは許容し、
        // 破棄は安全な地点でのみ行う。
    } else {
        trackPluginLru_.splice(trackPluginLru_.begin(), trackPluginLru_, it->second.lruIt);
    }
    if (!it->second.ok) {
        *outMesh = nullptr;
        return false;
    }
    *outMesh = &it->second.mesh;
    return true;
}

bool StaticMeshCache::TryGetPierAssemblyMesh(const std::string& folderUtf8, float heightM,
                                             std::vector<std::string>& log,
                                             const rs::render::Mesh** outMesh) {
    RefreshGeneration();
    const std::string key = MakePluginMeshKey(MakePluginSourceKey(folderUtf8, "") + "|" + std::to_string(heightM));
    std::unordered_map<std::string, TrackPluginClassification>::iterator it = pierAssemblyCache_.find(key);
    if (it == pierAssemblyCache_.end()) {
        PluginSession probe;
        std::vector<std::string> probeLog;
        rs::app::GetParsedPluginCache().LoadInto(probe, folderUtf8, probeLog, heightM);
        TrackPluginClassification cls;
        cls.ok = probe.plugin.ok && probe.plugin.hasAssembly;
        if (cls.ok) {
            cls.mesh = BakeSessionAssemblyMesh(probe);
        }
        it = pierAssemblyCache_.emplace(key, std::move(cls)).first;
        it->second.lruIt = pierAssemblyLru_.insert(pierAssemblyLru_.begin(), key);
        if (!it->second.ok) log.insert(log.end(), probeLog.begin(), probeLog.end());
        // ここでは破棄しない(PruneStaticMeshCache/PrunePierAssemblyCache の
        // コメント参照)。1フレーム内のリビルドで上限を一時的に超えることは許容し、
        // 破棄は安全な地点でのみ行う。
    } else {
        pierAssemblyLru_.splice(pierAssemblyLru_.begin(), pierAssemblyLru_, it->second.lruIt);
    }
    if (!it->second.ok) {
        *outMesh = nullptr;
        return false;
    }
    *outMesh = &it->second.mesh;
    return true;
}

const std::vector<rs::legacy_pi::SwitchDef>* StaticMeshCache::TryGetSwitchDefs(
    const std::string& folderUtf8, std::vector<std::string>& log) {
    RefreshGeneration();
    const std::string key = MakePluginSourceKey(folderUtf8, "");
    std::unordered_map<std::string, SwitchDefsClassification>::iterator it =
        switchDefsCache_.find(key);
    if (it == switchDefsCache_.end()) {
        PluginSession probe;
        std::vector<std::string> probeLog;
        rs::app::GetParsedPluginCache().LoadInto(probe, folderUtf8, probeLog);
        SwitchDefsClassification cls;
        cls.ok = probe.plugin.ok;
        if (cls.ok) cls.switches = probe.plugin.def.switches;
        it = switchDefsCache_.emplace(key, std::move(cls)).first;
        if (!it->second.ok) log.insert(log.end(), probeLog.begin(), probeLog.end());
        // LoadFolder済みのsessionを保持する。これにより同じ配置のスイッチをGUIで
        // 切り替えるたびにTryGetStaticInstanceMeshがディスク再読込を省ける(スイッチ切替
        // ヒッチ対策)。保持数はkMaxRetainedProbeSessionsで
        // 上限があるので、レイアウト規模でセッションが積み上がることはない。
        RetainProbeSession(key, std::move(probe));
    }
    if (!it->second.ok) return nullptr;
    return &it->second.switches;
}

const rs::legacy_pi::sidecar::StationSidecar& StaticMeshCache::TryGetStationSidecar(
    const std::string& folderUtf8) {
    RefreshGeneration();
    const std::string key = MakePluginSourceKey(folderUtf8, "");
    const std::unordered_map<std::string, rs::legacy_pi::sidecar::StationSidecar>::iterator it =
        stationSidecarCache_.find(key);
    if (it != stationSidecarCache_.end()) return it->second;
    rs::legacy_pi::sidecar::StationSidecar sidecar =
        rs::legacy_pi::sidecar::LoadStationSidecar(rs::platform_text::Utf8ToWide(folderUtf8));
    return stationSidecarCache_.emplace(key, std::move(sidecar)).first->second;
}

const rs::legacy_pi::sidecar::TrainSidecar& StaticMeshCache::TryGetTrainSidecar(
    const std::string& folderUtf8) {
    RefreshGeneration();
    const std::string key = MakePluginSourceKey(folderUtf8, "");
    const std::unordered_map<std::string, rs::legacy_pi::sidecar::TrainSidecar>::iterator it =
        trainSidecarCache_.find(key);
    if (it != trainSidecarCache_.end()) return it->second;
    rs::legacy_pi::sidecar::TrainSidecar sidecar =
        rs::legacy_pi::sidecar::LoadTrainSidecar(rs::platform_text::Utf8ToWide(folderUtf8));
    return trainSidecarCache_.emplace(key, std::move(sidecar)).first->second;
}


} // namespace rs::app

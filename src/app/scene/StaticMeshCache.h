//
// StaticMeshCache.h - slots_に依存しない静的メッシュ分類キャッシュ
// (SceneRenderer::TryGetStaticInstanceMesh/TryGetTrackPluginAssemblyMesh/
// MakeStaticInstanceKeyの実体)。
//
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ParsedPluginCache.h"
#include "StaticInstanceKey.h"
#include "StaticMeshVariantSet.h"
#include "../plugin_session/PluginSession.h"
#include "../../plugins/schema/sidecar/StationSidecar.h"
#include "../../plugins/schema/sidecar/TrainSidecar.h"
#include "../../render/mesh/Mesh.h"
#include "../../plugins/schema/SwitchDef.h"

namespace rs::app {

class StaticMeshCache {
public:
    ParsedPluginCache& ParsedPlugins() { return GetParsedPluginCache(); }
    uint64_t Generation() const { return GetParsedPluginCache().Generation(); }
    // 読込結果の世代が変わったとき、派生データをまとめて捨てる。
    bool RefreshGeneration();

    // folderUtf8+switchOptionsの組が静的配置(GPUインスタンス化)対象か判定する。
    // 同じ組み合わせは初回のみLoadFolder+ApplySwitchOptionsAndBakeして判定・結果を
    // キャッシュする(2回目以降はディスク再読込しない)。eligible==trueのときのみ
    // *outMeshへ(スイッチ確定値を焼き込み済みの)組立メッシュを返す
    // (EditorViewport.cppのInstanceBatchManager::EnsureBatchへそのまま渡す用)。
    // Sync()自身の判定にも使う(同じキャッシュを共有し二重ロードを避ける)。
    // _CAMDIST参照でバリアントが複数あるものは、最も近いバンドから見て最初の空でない
    // バリアントを返す(ピッキング・ハイライト用。全部空ならvariants[0])。
    // 同一folderでもswitchOptionsが異なれば別キャッシュ・別バッチにする必要がある
    // (PluginSession::ApplySwitchOptionsAndBakeのコメント参照)。
    // signalModelが真なら、信号のモデルの個体として判定する(駅舎の実行時スイッチ
    // _APPROACH1/2・_STOPPING を常に0とみなす。信号には列車の接近も停車も無い)。キャッシュも別になる。
    bool TryGetStaticInstanceMesh(const std::string& folderUtf8,
                                  const std::vector<int32_t>& switchOptions,
                                  std::vector<std::string>& log, const rs::render::Mesh** outMesh,
                                  bool signalModel = false, const std::string& definitionFile = std::string());

    // TryGetStaticInstanceMeshの拡張版。_CAMDISTを参照するプラグインも含め、
    // 静的インスタンス化できるものは_CAMDISTバンドごとに焼いたメッシュの集合を返す
    // (_CAMDIST非参照ならvariantsが1個)。ポインタの寿命はTryGetStaticInstanceMeshと同じ
    // (PruneStaticMeshCacheまで)。
    bool TryGetStaticInstanceVariants(const std::string& folderUtf8,
                                      const std::vector<int32_t>& switchOptions,
                                      std::vector<std::string>& log,
                                      const StaticMeshVariantSet** outVariants, bool signalModel = false, const std::string& definitionFile = std::string());

    // TrackPluginの離散反復配置(Pier等、EditorViewport.cpp)向けキャッシュ。
    // folderUtf8単位でLoadFolder結果(plugin.assemblyMesh)を1回だけ読み込み、以後の
    // 再配置/リビルドではディスク再読込しない(敷設エディタで編集のたびに
    // Pier等をLoadFolderし直すと操作が固まるため)。
    // TryGetStaticInstanceMeshと異なりIsFullyStatic()判定は行わず、switchOptionsも
    // 扱わない(TrackPluginPlacementItemはswitchOptionsを持たない。TrackPlugin
    // 配置ループの判定はok&&hasAssemblyとする)。
    bool TryGetTrackPluginAssemblyMesh(const std::string& folderUtf8,
                                      std::vector<std::string>& log,
                                      const rs::render::Mesh** outMesh);

    // Pier(橋脚)専用: folderUtf8 + 実際の設置点高さ(heightM、地面y=0からの距離)で
    // アセンブリメッシュをキャッシュする(TryGetTrackPluginAssemblyMeshと違い、
    // シャフト高さが配置点ごとに異なるためまとめたfolder単独キーでは共有できない)。
    // heightM は呼び出し側でquantizeStepM単位(目安0.25m)に丸めてから渡すこと
    // (キャッシュ爆発防止)。folder+丸めた高さの複合キーで trackPluginAssemblyCache_
    // とは別のマップにキャッシュする。
    bool TryGetPierAssemblyMesh(const std::string& folderUtf8, float heightM,
                                std::vector<std::string>& log, const rs::render::Mesh** outMesh);

    // folderUtf8のプラグインが宣言するスイッチ定義(DefineSwitch、宣言順)を返す。
    // 配置スイッチ表示パネル(読み取り専用)向け。初回のみLoadFolderして定義だけを
    // キャッシュし、2回目以降はディスクを読まない。読み込みに失敗した場合は
    // nullptrを返す。
    const std::vector<rs::legacy_pi::SwitchDef>* TryGetSwitchDefs(const std::string& folderUtf8,
                                                                   std::vector<std::string>& log);

    // folderUtf8のプラグインが持つstation.json(信号のsignal/駅発車標の運行情報KV)を
    // 返す。初回のみディスクから読み、以後はキャッシュを返す。ファイルが無い/パース
    // 失敗でもクラッシュせず、loaded==falseのStationSidecarを返す(呼び出し側は
    // loaded/hasSignal/tableで判定する。診断は戻り値のdiagnosticsに入っている)。
    const rs::legacy_pi::sidecar::StationSidecar& TryGetStationSidecar(
        const std::string& folderUtf8);

    // 同様にtrain.jsonを返す。
    const rs::legacy_pi::sidecar::TrainSidecar& TryGetTrainSidecar(
        const std::string& folderUtf8);

    // 静的メッシュキャッシュを上限まで切り詰める(最も長く使われていないものから破棄)。
    // TryGetStaticInstanceMesh()は`const Mesh*`を呼び出し側へ返し、呼び出し側は
    // 大量配置のリビルドで「全キー分のポインタを集めてから一括GPUアップロード」する。
    // そのため破棄を取得関数の中で行うと、同じループで先に集めたポインタが後続の
    // ミスによって破棄され宙に浮く(ユニークキー数が上限を超える規模のレイアウトで
    // 実際に起こりうる)。破棄は「外に出したポインタが1つも生きていない」と
    // 呼び出し側が確かめられる地点から明示的に呼ぶこと。
    //
    // liveKeysには「今そのレイアウトで実際に使われているキー」を渡す。ここに
    // 含まれるエントリは上限を超えていても破棄しない。上限をレイアウトの実キー数が
    // わずかに上回ると、リビルドのたびにLRU末尾を捨てては次のリビルドで作り直す
    // スラッシングになり、切替1回あたり数十msを浪費するため。
    // 上限は「使われていないキー(スイッチをスクラブして試した組み合わせ等)を
    // ためこまない」ためのものなので、live分は常に残す。
    void PruneStaticMeshCache(const std::unordered_set<std::string>& liveKeys);

    // trackPluginAssemblyCache_を上限まで切り詰める(最も長く使われていないものから
    // 破棄)。PruneStaticMeshCacheと同じ約束事: TryGetTrackPluginAssemblyMeshは
    // `const Mesh*`を呼び出し側へ返し、呼び出し側は大量配置のリビルドで
    // 「全キー分のポインタを集めてから一括GPUアップロード」する。そのため破棄を
    // 取得関数の中で行うと、同じループで先に集めたポインタが後続のミスによって
    // 破棄され宙に浮く。破棄は「外に出したポインタが1つも生きていない」と
    // 呼び出し側が確かめられる地点から明示的に呼ぶこと。liveKeysの扱いも
    // PruneStaticMeshCacheと同じ。
    void PruneTrackPluginAssemblyCache(const std::unordered_set<std::string>& liveKeys);

    // pierAssemblyCache_を上限まで切り詰める(最も長く使われていないものから破棄)。
    // 約束事はPruneTrackPluginAssemblyCacheと同じ(呼び出し側が安全な地点でのみ呼ぶ)。
    void PrunePierAssemblyCache(const std::unordered_set<std::string>& liveKeys);

    // [switchprof] 計測専用: 静的判定キャッシュの現在エントリ数。
    size_t StaticClassCacheSize() const { return staticClassCache_.size(); }

    // [switchprof] 計測専用: TryGetStaticInstanceMesh
    // のキャッシュミス時実ロード(ParsedPluginCache::LoadInto、SceneSlotSynchronizerの
    // switchProf.loadMsとは別経路のため別に計測する。大規模レイアウトの初期
    // Sync時間がpass1(Scenery分類)に大きく食われる原因の調査用)の累積時間・回数を取得し、内部
    // カウンタをリセットする。呼び出し側(SceneSlotSynchronizer::Sync)が1 Sync分の
    // 値をLogSwitchSyncProfileへまとめて出すために使う。
    double TakeProbeLoadMs() {
        const double v = probeLoadMs_;
        probeLoadMs_ = 0.0;
        return v;
    }
    int TakeProbeLoadCount() {
        const int v = probeLoadCount_;
        probeLoadCount_ = 0;
        return v;
    }
    int TakeProbeCacheHitCount() {
        const int v = probeCacheHitCount_;
        probeCacheHitCount_ = 0;
        return v;
    }

private:
    // TryGetStaticInstanceMesh()の判定結果キャッシュ(folderUtf8+switchOptions単位)。
    // eligible==falseのエントリはvariantsが空のまま。
    struct StaticClassification {
        bool eligible = false;
        StaticMeshVariantSet variants;
        // staticClassLru_ 内の自エントリへのイテレータ(最近使用順の付け替え用)。
        std::list<std::string>::iterator lruIt;
    };
    // このマップに保持できるエントリ数の上限。スイッチ組み合わせをGUIで
    // スクラブできるため、evictionが無いと大型情景
    // プラグインのコンボ探索だけでメッシュ丸ごとのエントリが無制限に積み上がる。
    static constexpr size_t kMaxStaticClassEntries = 256;
    // 最近使用順(先頭が最新)。エントリのkeyのみを保持し、実体はstaticClassCache_側。
    std::list<std::string> staticClassLru_;
    std::unordered_map<std::string, StaticClassification> staticClassCache_;

    uint64_t generation_ = 0;

    // TryGetStaticInstanceMesh()のミス処理本体。既にLoadFolder済みのsession(保持
    // セッション、またはこの呼び出し限りのローカルセッション)を受け取り、eligible
    // 判定・バンド別のスイッチ焼き込み・パートワールド焼き込み・AABB計算までを行った結果を
    // 返す。呼び出し元(保持セッション経路/ローカルセッション経路)でロジックを
    // 重複させないための共通化。
    StaticClassification ClassifyStaticSession(PluginSession& session,
                                               const std::vector<int32_t>& switchOptions,
                                               bool stationRuntimeConstant);

    // folderUtf8単位で読み込み済みPluginSessionを保持する小さなLRU。
    // TryGetSwitchDefs()(配置スイッチ編集UI)と、TryGetStaticInstanceVariants()の判定で
    // 新しく読んだsessionを格納する。後者は同じフォルダをスイッチ値違いで多数配置する
    // レイアウトで、組ごとの読込結果の複製を省くため。全フォルダ分を保持すると大規模
    // レイアウトでは数百セッションが常駐してしまうため上限を絞る。
    static constexpr size_t kMaxRetainedProbeSessions = 6;
    // 最近使用順(先頭が最新)。folderUtf8→PluginSessionの実体を持つ。
    std::list<std::pair<std::string, PluginSession>> retainedProbeSessions_;
    std::unordered_map<std::string, std::list<std::pair<std::string, PluginSession>>::iterator>
        retainedProbeIndex_;

    // folderUtf8のPluginSessionを保持LRUへ格納(既存なら置き換えて最新化)する。
    // 上限超過時は最も長く使われていないエントリを破棄する。
    void RetainProbeSession(const std::string& folderUtf8, PluginSession&& session);
    // 保持LRUからfolderUtf8のPluginSessionを取り出す(見つかれば最近使用順の先頭へ
    // 付け替える)。見つからなければnullptrを返す。
    PluginSession* FindRetainedProbeSession(const std::string& folderUtf8);

    // TryGetTrackPluginAssemblyMesh()のキャッシュ(folderUtf8単位)。
    struct TrackPluginClassification {
        bool ok = false;
        rs::render::Mesh mesh;
        // trackPluginLru_ 内の自エントリへのイテレータ(最近使用順の付け替え用)。
        std::list<std::string>::iterator lruIt;
    };
    // このマップ・pierAssemblyCache_に保持できるエントリ数の上限。
    // staticClassCache_と同じ理由(kMaxStaticClassEntriesのコメント参照)で
    // evictionが必要なため、同じ値を流用する。
    static constexpr size_t kMaxTrackPluginAssemblyEntries = kMaxStaticClassEntries;
    static constexpr size_t kMaxPierAssemblyEntries = kMaxStaticClassEntries;
    // 最近使用順(先頭が最新)。エントリのkeyのみを保持し、実体はtrackPluginAssemblyCache_側。
    std::list<std::string> trackPluginLru_;
    std::unordered_map<std::string, TrackPluginClassification> trackPluginAssemblyCache_;

    // TryGetPierAssemblyMesh()のキャッシュ(folderUtf8+丸めた高さの複合キー単位)。
    // 最近使用順(先頭が最新)。エントリのkeyのみを保持し、実体はpierAssemblyCache_側。
    std::list<std::string> pierAssemblyLru_;
    std::unordered_map<std::string, TrackPluginClassification> pierAssemblyCache_;

    // TryGetSwitchDefs()のキャッシュ(folderUtf8単位)。
    struct SwitchDefsClassification {
        bool ok = false;
        std::vector<rs::legacy_pi::SwitchDef> switches;
    };
    std::unordered_map<std::string, SwitchDefsClassification> switchDefsCache_;

    // TryGetStationSidecar()/TryGetTrainSidecar()のキャッシュ(folderUtf8単位)。
    std::unordered_map<std::string, rs::legacy_pi::sidecar::StationSidecar> stationSidecarCache_;
    std::unordered_map<std::string, rs::legacy_pi::sidecar::TrainSidecar> trainSidecarCache_;

    // [switchprof] 計測専用: TakeProbeLoadMs/TakeProbeLoadCount参照。
    double probeLoadMs_ = 0.0;
    int    probeLoadCount_ = 0;
    int    probeCacheHitCount_ = 0;
};

} // namespace rs::app

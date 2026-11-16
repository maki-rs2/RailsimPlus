#pragma once

// SceneSlotSynchronizer::Sync()の実装を複数TUへ分割するための内部専用ヘッダ。
// SceneSlotSynchronizer.h(SceneRenderer.h経由で広く波及する)には入れず、
// SceneSlotSynchronizer.cpp / SceneSlotSynchronizerResolve.cpp /
// SceneSlotSynchronizerUpload.cpp の3TUだけがincludeする。

#include <cstdint>
#include <string>
#include <vector>

#include "SceneSlotSynchronizer.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

class App;
class StaticMeshCache;

// [switchprof] 計測専用スキャフォールド。環境変数
// RS_PROFILE_LOAD 指定時のみ、Sync()内の内訳を1回のログにまとめる。
struct SwitchSyncProfile {
    double bakeMs = 0.0;
    int    bakeCount = 0;
    double loadMs = 0.0;
    int    loadCount = 0;
    int    cacheHitCount = 0;
    // cacheHit時のLoadFromParsed(=plugin=parsedの値コピー)の所要時間。調査用
    // 一時計測: loadMsはLoadFolder(ディスクI/O+パース)のみを計測しており、
    // cacheHitパスのLoadedPlugin丸ごとコピーは別に計測しないと埋もれるため、
    // sync合計とbake+load+upload合計の間の差分の原因を切り分ける。
    double copyMs = 0.0;
    // 新規スロットのGPU常駐リソース確保(EnsureSceneSlotCapacity→MeshPipeline::Init、
    // 定数バッファ/gPartWorld/InstanceMatOverrideのCreateCommittedResource×6)の所要
    // 時間。調査用一時計測(copyMsと同じ切り分け目的)。
    double ensureCapMs = 0.0;
    // pass1(revive-exact判定)・pass2(unresolved処理全体)・dedup(Trainバッチ
    // 事前重複排除)の各区間丸ごとの所要時間。調査用一時計測。
    // pass2Msはload/bake/copy/ensureCapMsを含む(差分が「その他」の目安になる)。
    double pass1Ms = 0.0;
    double pass2Ms = 0.0;
    double dedupMs = 0.0;
    double uploadMs = 0.0;
    // pass1中にStaticMeshCache::TryGetStaticInstanceMeshが行うScenery分類probeの
    // 読込(キャッシュミス時のみ)。pass1Msの大半を占めうるため内訳として別に持つ
    // (StaticMeshCache::TakeProbeLoadMs/TakeProbeLoadCount参照)。
    double staticProbeLoadMs = 0.0;
    int    staticProbeLoadCount = 0;
    int    staticProbeCacheHitCount = 0;
    int    reviveExact = 0;
    int    reviveFolder = 0;
    int    reviveNew = 0;
    // フォルダ一致の再利用で、テクスチャだけが変わったためGPUのメッシュ(VB/IB)を
    // 上げ直さずに済ませた件数(bakeCountの内数)。
    int    meshSkipCount = 0;
    // meshSkipCountのうち、マテリアルのテクスチャ割当を実際に差し替えた個数の合計。
    int    textureRebindCount = 0;
};

void LogSwitchSyncProfile(const SwitchSyncProfile& prof, double syncMs);

struct PendingUpload {
    size_t slotIndex;
    rs::world::Guid entityId = rs::world::NilGuid();
    std::string folderUtf8;
    std::string geometryKey;
};

constexpr size_t kSceneSlotUploadsPerSubmission = 8;

// レイアウト読込・Scene切替時のフレーム分割ジョブ(SceneSlotSynchronizer::Sync内の
// RunLoadJobBudget)が1回のSync()呼び出しで処理してよい時間予算(ミリ秒)。
// 「少しずつ処理を譲りながらフレーム分割する」方式だが、
// 1件あたりのコスト(Scenery分類probe・LoadFolder・ApplySwitchOptionsAndBake・GPU
// アップロードいずれも)のばらつきが非常に大きく、件数固定のチャンクだと軽い件の
// 連続では待ち過ぎに、重い件が混じると数秒規模のヒッチになる。そのため件数ではなく
// 経過時間で1回のSync()を打ち切る方式にする。ただし1回のSync()呼び出しでは
// 予算超過でも最低1件は処理する(重い1件が予算を超えてもジョブが進まなくなる
// ことを避ける)。
constexpr double kSceneLoadFrameBudgetMs = 80.0;

// スロット再利用でテクスチャプールを保持し続けるために要求する残枠。新しい
// スイッチ値で必要になるテクスチャ数はbake前には分からないため、1エンティティ分の
// 追加を賄える余裕を定数で見込む(足りなければプールを破棄して枠を巻き戻す)。
constexpr UINT kReuseTexSlotHeadroom = 64;

// 前方宣言(SyncContextの定義より前に関数宣言を置くため)。
struct SyncContext;

// フォルダ一致のparkedスロットを再利用するときの、スイッチの焼き直しとGPU側の解放
// (SceneSlotSynchronizerRebake.cpp)。Scenery(車両以外)で、テクスチャだけが変わる
// スイッチ変更なら、GPUのメッシュを残したままテクスチャを差し替える。
// 戻り値: true=GPUのメッシュを残した(呼び出し側はアップロードを積まない)。
// false=GPU側を解放済み(呼び出し側がメッシュを上げ直す)。
bool RebakeReusedSlot(SyncContext& ctx, const rs::world::SceneEntity& entity, size_t slotIndex);

// slotのsessionへentityのスイッチ確定値を焼き込む(ApplySwitchOptionsAndBake)。
// プロファイル有効時は所要時間と回数を ctx.profile.bakeMs/bakeCount へ足す。
// 再利用経路(RebakeReusedSlot)と新規読込経路(ResolveOneEntity)で共有する。
void BakeSwitchOptionsWithProfile(SyncContext& ctx, SceneSlotPool::Slot& slot,
                                  const rs::world::SceneEntity& entity);

// SceneSlotSynchronizer::Sync()の各ステージが持ち回る状態。ステージ間で
// Slot&/Slot*等の要素参照は持たない(GrowSlot()のpush_backで無効化されるため、
// 持つのはslotsベクタ自体への参照のみ)。パス1で見つかった未解決entityは
// (このstructではなく)loadJob_.unresolvedIdsへ直接id文字列で追記する
// (id文字列で持つ理由はLoadJobStateのコメント参照)。
struct SyncContext {
    App& app;
    const rs::world::SceneRegistry& registry;
    StaticMeshCache& staticMeshCache;
    const ExternalSceneMeshes& externalMeshes;
    std::vector<std::string>& log;
    std::vector<SceneSlotPool::Slot>& slots;
    uint64_t syncRevision;
    bool profileEnabled;
    SwitchSyncProfile& profile;
    std::vector<PendingUpload>& pendingUploads;
};

} // namespace rs::app

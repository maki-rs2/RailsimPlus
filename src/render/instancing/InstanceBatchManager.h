//
// InstanceBatchManager.h - basePath単位の静的シーナリーGPUインスタンス描画バッチ
//
// 内部実装は`TrainInstanceBatch`系(車両用)への薄いfacadeにしてある
// (`InstanceBatch`と`TrainInstanceBatch`の実質2重実装を避けるため)。
// `partsPerInstance=1`・マテリアルオーバーライド空で運用すれば、車両用の
// `TrainInstanceBatchManager`がそのまま静的シーナリー用途も扱えるため、
// この公開APIはそのまま維持しつつ`impl_`(TrainInstanceBatchManager)へ委譲する。
#pragma once

#include <d3d12.h>

#include <ResourceUploadBatch.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "TrainInstanceBatchManager.h"
#include "../mesh/Mesh.h"
#include "../pipeline/MeshPipelineSharedState.h"

namespace rs::render {

// basePath をキーにGPUインスタンス描画バッチを管理する(実体は`impl_`)。
// シーン編集UI/SceneRegistry との自動連携は持たない
// (呼び出し側がAddInstance/EnsureBatchを明示的に呼ぶ)。
class InstanceBatchManager {
public:
    // firstSrvIndex から srvPoolCapacity 分のSRVレンジを`impl_`が管理する
    // (呼び出し側がレンジを予約済みであること。`trainInstanceBatches_`とは別レンジを
    // 渡すこと、プールを共有すると静的/車両バッチが枯渇を奪い合う)。sharedは呼び出し側が
    // 1回だけInit済みの共有root signature/PSO一式(全バッチが共有参照する、
    // MeshPipelineSharedState.h参照)。
    bool Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
              ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT firstSrvIndex,
              UINT srvPoolCapacity, UINT batchMaxTextures, UINT batchInitialInstances);

    // basePath のバッチが無ければ新設して mesh をアップロードする(GeometryManager
    // 経由でVB/IBはbasePath単位に共有される、partsPerInstance=1・動的マテリアル
    // スロット無しで`impl_`へ委譲)。SRVプール枯渇時は新設できずfalseを返す。既存
    // バッチがあれば mesh 引数は無視しそのまま true を返す。basePath単位で一度失敗
    // すると、`failedBatchKeys_`により以後同じbasePathでの再試行は即falseを返す
    // (再試行のたびに警告ログが再発するのを防ぐ、SceneTrainBatchingと同じ抑制方針)。
    bool EnsureBatch(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                     const std::string& basePath, const Mesh& mesh);

    // EnsureBatch(basePath, mesh) が実処理(新設)になる場合に限り、mesh のマテリアルが
    // 参照するテクスチャパス(base/normal/orm)を out へ追加する。既にバッチがある/
    // 失敗したキーは何も足さない。共有の TextureManager に GPU リソースが生存中の組と
    // 恒久失敗の組も、登録時に画像を読まないので足さない。
    // 複数バッチ分を集めて PrefetchImages へ1回で渡し、デコードをバッチ横断で並列化する用途。
    void CollectNewBatchTexturePaths(const std::string& basePath, const Mesh& mesh,
                                     std::vector<std::string>& out) const;

    // basePath のバッチがGPU側に作成済みか(EnsureBatchが実処理にならないか)の判定。
    bool HasBatch(const std::string& basePath) const { return impl_.HasBatch(basePath); }

    // basePath のバッチへ1配置を追加する(ワールド行列1個をPartXformへ包んで
    // `impl_`へ委譲。静的シーナリー[平行移動/回転/一様スケール前提]の近似として
    // 法線行列にもワールド行列をそのまま使う)。EnsureBatch 未実行なら false。
    bool AddInstance(const std::string& basePath, const float world[16]);

    // 全バッチの配置だけを空にする。GPU側のジオメトリ/テクスチャ(basePathのバッチ自体、
    // SRVレンジ)はそのまま維持し、破棄・再アップロードは発生しない。呼び出し側が毎回
    // EnsureBatch+AddInstanceで全件を積み直す「配置リストのリビルド」用途
    // (編集のたびに全バッチを破棄・再構築すると、無関係なバッチまで
    // 含め毎回GPUへ全ジオメトリ/テクスチャを再アップロードし直すことになり、編集操作が
    // 固まるため)。
    void ClearInstances();

    // keys に含まれるバッチの配置だけを空にする(存在しないキーは無視)。_CAMDISTのバンドが
    // 変わった配置を含むバッチだけを積み直す用途。GPU側のジオメトリ・SRVには触れない。
    void ClearInstances(const std::unordered_set<std::string>& keys);

    // liveKeys に含まれない basePath のバッチを即座には解放しない(`impl_.
    // PruneUnusedCapped(liveKeys, cap)`。Scene切替で一時的に非表示になった静的
    // シーナリーのGPUバッチをpark=SRVレンジ・ジオメトリ保持したまま残し、cap件数を
    // 超えた分だけ最も長くliveでないものから実解放する。capは`RenderTuning::
    // Embedded().staticInstanceBatchResidencyCap`。
    // `failedBatchKeys_`は同様にliveKeysに含まれなくなった時点で忘れる
    // (配置削除後に同名basePathが別の意味で再利用される場合など、古い失敗記録が
    // 居座り続けないようにする。parkとは別の負キャッシュなので即時破棄のままでよい)。
    void Prune(const std::unordered_set<std::string>& liveKeys);

    // 全バッチの gPartWorld をフレーム f へ反映する(Render() 前に毎フレーム呼ぶ)。
    void Sync(UINT frameIndex);

    // 全バッチの不透明+カットアウトsubmeshを描画する。
    void RecordOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base,
                      UINT frameIndex);

    // 全バッチのBlended(材質色Diffuse.a<1の窓ガラスなど)submeshを描画する。全パイプラインの
    // 不透明描画の後、RecordLateOpaqueより前に呼ぶ。
    void RecordBlended(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base,
                       UINT frameIndex);

    // 全バッチのLateOpaque(PI`Transparent`指定)submeshを描画する。全パイプラインの
    // 不透明描画の後に呼ぶ。
    void RecordLateOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base,
                          UINT frameIndex);

    // 全バッチの半透明submeshをWBOITパスへ描画する(WBOITブラケット内かつWBOIT
    // 有効時のみ呼ぶこと)。
    void RecordTransparent(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base,
                           UINT frameIndex);

    // 全バッチを太陽光の影パスへ描く(不透明+カットアウトのみ)。lightFrustum を渡すと、
    // どのインスタンスも入らないバッチは飛ばす。
    void RecordShadow(const rs::rhi::CommandList& commandList, const MeshConstants& lightConstants,
                      UINT frameIndex, UINT shadowSlice,
                      const rs::core::Frustum* lightFrustum = nullptr);

    // 全バッチを主視点の深度プリパスへ描く。
    void RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex);

    size_t BatchCount() const { return impl_.BatchCount(); }
    uint64_t TotalInstanceCount() const;

private:
    TrainInstanceBatchManager impl_;
    // EnsureBatchが一度失敗したbasePath。以後このキーはEnsureBatchの実処理を
    // スキップして即falseを返す(再試行のたびに警告ログが再発するのを防ぐ)。
    // `TrainInstanceBatchManager`側は負キャッシュを持たないため、ここで保持する。
    std::unordered_set<std::string> failedBatchKeys_;
};

} // namespace rs::render

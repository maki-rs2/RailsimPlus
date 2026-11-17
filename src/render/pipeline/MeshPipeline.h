//
// mesh_pipeline.h - DX12 メッシュ描画パイプライン
//
// POSITION/NORMAL/TEXCOORD 入力、定数バッファ(MVP/法線行列/光/カメラ)、
// Lambert + アンビエント + ベーステクスチャの PS、D32 深度、バックフェースカリング。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../mesh/Mesh.h"
#include "../postprocess/DepthProbe.h"
#include "../rhi/CommandList.h"
#include "../rhi/SrvHeap.h"
#include "../resource/TextureManager.h"
#include "../resource/TextureResource.h"
#include "MeshConstantBuffer.h"
#include "MeshDynamicBuffers.h"
#include "MeshGeometry.h"
#include "MeshMaterialSet.h"
#include "MeshPipelineSharedState.h"
#include "MeshPipelineTypes.h"
#include "MeshTexturePool.h"
#include "MeshTransparency.h"

namespace rs::render {

enum class ShadowCasterKind; // SubmeshAlphaClassify.h

class MeshPipeline {
public:
    // srvHeap は shader-visible。textureHeapStart 以降の descriptor をテクスチャに使う。
    // shared は呼び出し側(FrameRenderer)が1回だけ Init 済みの root signature/PSO一式
    // (全MeshPipelineインスタンス間で共有し、per-slot/per-batchの重複生成を避ける。
    // MeshPipelineSharedState.h参照)。shared のポインタはこのMeshPipelineの
    // 生存期間中ずっと有効であること(所有者=FrameRenderer側の寿命が上回る前提)。
    // textureManager はテクスチャのGPUリソースの取得先。本編は共有の GetTextureManager()
    // を渡す。共有のものはアップロード完了前の資源をキャッシュへ入れるため、別キューで
    // 動くワーカー(サムネイル撮影)は専用のものを渡す(メインが完了前の資源を読まないように)。
    // こちらも生存期間中ずっと有効であること。
    bool Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
              ID3D12DescriptorHeap* srvHeap, UINT srvDescriptorSize,
              UINT firstTextureDescriptorIndex, UINT maxTextures,
              UINT firstTransBufDescriptorIndex,
              UINT firstPartWorldDescriptorIndex, UINT firstInstanceMatOverrideDescriptorIndex,
              UINT maxParts, TextureManager& textureManager);

    // メッシュを GPU へアップロード（頂点/インデックス/マテリアルテクスチャ）。
    // geometryKey が空の場合: 頂点/インデックスバッファは UPLOAD ヒープへ直接確保
    // して即座に使う(コピーコマンド不要)。
    // geometryKey が非空の場合: GeometryManager 経由で basePath(=geometryKey)単位に
    // dedup された DEFAULT ヒープの共有頂点/インデックスバッファを使う(同一
    // geometryKey が複数の MeshPipeline から渡されると GPU バッファを共有する)。
    // マテリアルテクスチャは upload(ResourceUploadBatch)側へ積む(呼び出し側が
    // Begin() 済みで、UploadMesh() の後に End() を呼ぶこと)。
    bool UploadMesh(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                    const Mesh& mesh,
                    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive,
                    const std::string& geometryKey = std::string());

    // 深度バッファ（RTV サイズ変更時に再生成）
    bool CreateDepthBuffer(ID3D12Device* device, UINT width, UINT height);
    // 深度バッファのSRV(R32_FLOAT)を書く。作り直した(CreateDepthBuffer)あとに呼び直す。
    bool WriteDepthSrv(const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex) const;
    // 深度を読み取り用(DEPTH_READ|SRV)と書き込み用(DEPTH_WRITE)の間で遷移させる。
    void TransitionDepth(const rs::rhi::CommandList& commandList, bool toShaderRead) const;
    // 深度(書き込み状態)の画素(x, y)を読み戻し用バッファへ写す。DepthProbe の使い方に従う。
    bool RecordDepthProbe(const rs::rhi::CommandList& commandList, unsigned x, unsigned y);
    bool PollDepthProbe(float& depth) { return depthProbe_.Poll(depth); }

    const std::vector<PartXform>& PartXforms() const { return dynamicBuffers_.PartXforms(); }
    // 直近の UpdatePartWorlds で、前回とワールド行列が1つでも違ったか(動いているパーツがあるか)。
    bool PartWorldsMoving() const { return dynamicBuffers_.PartWorldsMoving(); }
    // フレーム frameIndex の gPartWorld にある前フレーム分の行列の先頭の要素番号(0なら無い)。
    UINT PrevPartWorldOffset(UINT frameIndex) const { return dynamicBuffers_.PrevPartWorldOffset(frameIndex); }

    // 描画。cbData は 1 フレーム分のカメラ/ライト設定（マテリアル色は内部で上書き）。
    // gPartWorld(UpdatePartWorldsで事前に書いたinstanceCount個のワールド行列)を
    // SV_InstanceID+partIndexで引くVSMainInstancedMultiPart/
    // psoInstancedMultiPartSolid_・psoInstancedMultiPartCutout_を使い、submeshごとに
    // DrawIndexedInstanced(indexCount, instanceCount, ...)を1回発行する(配置数に
    // 関わらずドローコールはsubmesh数だけ)。単体オブジェクト表示はinstanceCount=1・
    // partsPerInstance=そのメッシュ1体ぶんのパーツ(Joint)数として同じ経路を使う。
    // 半透明submeshはこの経路では描画しない
    // (RecordTransparentInstancedMultiPartが別途描く)。LateOpaqueもこの経路では描かず、
    // RecordLateOpaqueInstancedMultiPartが全パイプラインの不透明描画後に描く。
    // instanceCount==0なら何もしない。
    // インスタンス描画の半透明三角形パス。wboit=trueならWBOIT(2RT出力の
    // PSMainTransWBOIT)、falseならソート版フォールバック(奥→手前distanceソート)を使う。
    // 全半透明三角形をDrawIndexedInstanced 1回で描く。半透明三角形が無ければ何もしない。
    void RecordInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                  const MeshConstants& baseConstants, UINT frameIndex,
                                  UINT instanceCount, UINT partsPerInstance);
    // Blended(材質色Diffuse.a<1の窓ガラスなど)submeshを、submesh順に1パスで描く。フレーム内の
    // 全パイプラインのRecordInstancedMultiPartを積んだ後、LateOpaqueより前に呼ぶ。
    // 同フレームで本パイプラインのRecordInstancedMultiPartを呼んでいなければ何もしない。
    void RecordBlendedInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                         const MeshConstants& baseConstants,
                                         UINT frameIndex, UINT instanceCount, UINT partsPerInstance);
    // LateOpaque(PI`Transparent`指定)submeshを深度専用→カラーの2段で描く。フレーム内の
    // 全パイプラインのRecordInstancedMultiPartを積んだ後に呼ぶ(RailSim II の全物体描画後の
    // まとめ描きに相当)。同フレームで本パイプラインのRecordInstancedMultiPartを
    // 呼んでいなければ(フェード描画のみの場合を含む)何もしない。
    void RecordLateOpaqueInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                            const MeshConstants& baseConstants,
                                            UINT frameIndex, UINT instanceCount,
                                            UINT partsPerInstance);
    void RecordTransparentInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                             const MeshConstants& baseConstants,
                                             UINT frameIndex, UINT instanceCount,
                                             UINT partsPerInstance, bool wboit);
    void RecordFadeInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                      const MeshConstants& baseConstants,
                                      UINT frameIndex, UINT instanceCount,
                                      UINT partsPerInstance);

    // Scenery選択ハイライト用: このメッシュ(gPartWorld[0]
    // へ事前にUpdatePartWorlds({worldMatrix})済みであること)を、tintColor
    // (rgb=塗り色, a=濃さ)でフラット単色描画する。VSMainInstanced+
    // psoInstancedHighlight_(GREATER_EQUAL、深度書込OFF)を使い、同一メッシュ・
    // 同一ワールド行列で不透明描画の上に1インスタンスだけ重ね描きする
    // (同一VSブロブ+同一頂点データ+同一ワールド行列なら深度値は
    // ビット単位で一致するため、共面Zファイティングは起きない)。半透明submesh
    // は対象外。indexCount_==0なら何もしない。
    void RecordHighlightInstanced(ID3D12GraphicsCommandList* cmdList,
                                  const MeshConstants& baseConstants, UINT frameIndex,
                                  const float tintColor[4]);

    // 非静的Scenery(SceneRenderer::slots_側、可動部/Joint追従アニメ持ち)選択
    // ハイライト用。instanceCount=1で
    // VSMainInstancedMultiPartを使い(instanceId=0のためgPartWorld添字は実質
    // partIndexのみで決まる)、SceneRenderer::Tickが毎フレーム更新済みの自身の
    // gPartWorldをそのまま再利用する(このメッシュ自体が既に個別描画されている
    // 前提のため、UpdatePartWorldsの呼び直しは不要。同一VSブロブ+同一頂点データ+
    // 同一gPartWorldで深度値がビット単位で一致するため、通常描画の直後に
    // 重ね描きしても共面Zファイティングは起きない)。半透明submeshは対象外。
    // indexCount_==0なら何もしない。
    void RecordHighlightSelf(ID3D12GraphicsCommandList* cmdList,
                             const MeshConstants& baseConstants, UINT frameIndex,
                             const float tintColor[4]);

    // シャドウパス: 影を落とす submesh(不透明・穴あき。半透明と後描きは除く)を、深度だけのPSOで描く。
    // lightConstants.viewProj にライトのクリップ座標への行列を入れて渡す。ルートシグネチャ・記述子テーブル・
    // 頂点/インデックスの張り方は通常の描画(BindInstancedMultiPart)と共通。定数バッファは通常の描画と別に持つ
    // (同じフレームで通常の描画が上書きするのを避けるため)。shadowSlice はカスケードの番号で、
    // 枚ごとに定数バッファの別のスロットを使う(同じフレームで枚数ぶん呼ぶので、共有すると最後の枚の行列で上書きされる)。
    void RecordShadowInstancedMultiPart(const rs::rhi::CommandList& commandList,
                                        const MeshConstants& lightConstants, UINT frameIndex,
                                        UINT shadowSlice, UINT instanceCount, UINT partsPerInstance, bool includeThin = true);

    // 主視点の深度プリパス: 本編で深度を書く不透明系の submesh(窓ガラス・半透明は除く)を、深度だけの
    // PSO で描く。constants は本編と同じ視点の定数。呼び出し側が深度専用の出力先(色なし)を張ってから呼ぶ。
    // 定数バッファは影パスと同じものの、影のカスケードぶんの後ろのスロットを使う(本編の定数バッファは触らない)。
    void RecordPrepassInstancedMultiPart(const rs::rhi::CommandList& commandList, const MeshConstants& constants,
                                         UINT frameIndex, UINT instanceCount, UINT partsPerInstance);

    // 速度パス: 動いているパーツの「前フレームの位置から今の位置への動き」を、速度の RT へ書く。
    // 呼び出し側が、速度の RT とプリパスの深度(読み取り専用)を出力先に張ってから呼ぶ。プリパスと同じ submesh を、
    // プリパスと同じ深度の面にだけ描く。前フレーム分の行列が無い(動いていない)ときは何も描かない。
    // 定数バッファは影パスと同じものの、プリパスのスロットの後ろを使う。
    void RecordVelocityInstancedMultiPart(const rs::rhi::CommandList& commandList, const MeshConstants& constants,
                                          UINT frameIndex, UINT instanceCount, UINT partsPerInstance);

    // パーツ単位ワールド行列を gPartWorld へ書き込む。空なら恒等行列1個を
    // partIndex=0 として書く（単一メッシュ表示との後方互換）。
    // RecordInstancedMultiPart 用途では xforms[instanceId*partsPerInstance+partIndex]
    // が該当インスタンス・パーツのワールド行列になる。
    // continuityKey は xforms の並びが前回と対応しているかを決めるキー(MeshDynamicBuffers 参照)。
    void UpdatePartWorlds(const std::vector<PartXform>& xforms, UINT frameIndex,
                          uint64_t continuityKey = 0);
    bool EnsureInstanceMatOverrideCapacity(ID3D12Device* device, UINT requiredOverrides,
                                           UINT frameIndex);
    void UpdateInstanceMatOverrides(const std::vector<InstanceMatOverride>& overrides,
                                    UINT frameIndex);
    void ConfigureDynamicMaterialSlots(const std::vector<uint32_t>& materialIndices);

    // gPartWorld バッファを requiredParts 個以上へ拡張する(frameIndex のフレーム分
    // のみ)。既に足りていれば何もしない。拡張時は新バッファを作り直して SRV を
    // 書き換えるため、「そのフレーム番号のGPU実行が完了済み」であること(=通常の
    // フレーム開始後)が前提。UpdatePartWorlds の直前に呼ぶこと。
    // インスタンス描画で配置数が初期容量を超えても取りこぼさないためのもので、
    // 逆に初期容量を小さく取れるので大量バッチ時のUPLOADヒープ消費も下がる。
    bool EnsurePartWorldCapacity(ID3D12Device* device, UINT requiredParts, UINT frameIndex);

    // --- ChangeTexture/ChangeMaterial の実行時反映 ---

    // path(base/normal/pbr の絶対解決済みパス、base は必須)を空きスロットへ
    // ロードし、以後 UpdateMaterialTexture() に渡す texSlotOffset・hasNormal・
    // hasOrm を infoOut へ返す(いずれもプール側から導出した値。呼び出し側の
    // パス文字列から独自に決めないこと)。同一 base パスは再ロードせずキャッシュ
    // 済みの値を返す。失敗時は false（呼び出し側はテクスチャ無し扱いにできる）。
    // maxTextures_ 上限に達した場合も false を返し infoOut は変更しない。
    bool PreloadTexture(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                        const std::string& basePath, const std::string& normalPath,
                        const std::string& pbrPath, PreloadedTexInfo& infoOut);

    // PreloadTexture 済みかどうかをコマンドリスト無しで調べる（毎フレームの
    // 実行時切替で GPU コマンド発行を避けるためのキャッシュ参照）。
    bool FindPreloadedSlot(const std::string& basePath, const std::string& normalPath,
                           const std::string& pbrPath, PreloadedTexInfo& infoOut) const;

    void ApplyInstanceMaterialTexture(InstanceMatOverride& value, bool hasTexture,
                                      const PreloadedTexInfo& info) const;

    // basePath が読込失敗で「失敗が確定」と判定済みかどうか。呼び出し側
    // (毎フレームの実行時切替)はこれが true の間、RunUploadCommands を呼ばず
    // スキップすべき(毎フレームの無駄な提出を避けるため)。失敗キャッシュは TextureManager 側でパイプライン横断に一元管理
    // する(basePath の読込失敗はどのパイプラインから見ても同じ結果になるため)。
    bool TexKnownFailed(const std::string& basePath) const {
        return texturePool_.TexKnownFailed(basePath);
    }

    // basePath がこのパイプラインのテクスチャ枠(maxTextures_)超過で登録できないと
    // 判明済みかどうか。TexKnownFailed と違いパイプライン固有(枠の大きさが
    // 用途ごとに異なるため)。呼び出し側は TexKnownFailed と同様、これが true の間は
    // PreloadTexture のフォールバックを呼ばずスキップする。
    bool TexBudgetExhausted(const std::string& basePath, const std::string& normalPath,
                            const std::string& pbrPath) const {
        return texturePool_.TexBudgetExhausted(basePath, normalPath, pbrPath);
    }

    // materialSet_ の materialIndex の色/ライティング値を書き換える（テクスチャ
    // パスは変更しない。texturePath 系フィールドは無視される）。
    // Record() が次フレームから参照するほか、半透明用の CPU 定数配列にも即時反映
    // する（GPU バッファへは Record() がフレームごとに memcpy で反映する）。
    void UpdateMaterialColor(uint32_t materialIndex, const Material& src);

    // materialIndex の使用テクスチャを texSlotOffset（PreloadTexture の戻り値、
    // または元メッシュの materialSet_.Textures()[materialIndex].texSlotOffset）へ切り替える。
    // hasTexture=false ならテクスチャ無し扱いに戻す。
    void UpdateMaterialTexture(uint32_t materialIndex, bool hasTexture,
                               uint32_t texSlotOffset, bool hasNormal, bool hasOrm);

    // materialIndex のテクスチャを UploadMesh() 直後(=真の .x ベース状態。
    // trig.trigger==Always な ChangeTexture のみ反映済み)へ明示的に戻す。
    // MaterialRuntime の Apply() で該当マテリアルの ChangeTexture がどれも
    // active でなくなったとき、呼び出し側(main.cpp)が毎フレーム呼ぶ用途。
    // (呼ばずに「触らない」でいると、直前に active だった Case のテクスチャに
    //  止まったまま戻らなくなる)
    void RevertMaterialTexture(uint32_t materialIndex);

    // メッシュを上げ直さずに、materialIndex のテクスチャを「UploadMesh 時点から
    // こうだった」ことにして差し替える。UpdateMaterialTexture に加えて
    // RevertMaterialTexture の戻り先(UploadMesh 直後のスナップショット)も同じ割当へ
    // 書き換える。UpdateMaterialTexture だけだと、次に ChangeTexture が非activeに
    // なったとき RevertMaterialTexture が古いテクスチャへ巻き戻してしまう。
    // 呼び出し側は、新しいテクスチャが FindPreloadedSlot でヒットすること、および
    // 差し替えで透明分類が変わらないこと(hasAlpha/hasZeroAlpha/validが同じ)を
    // 確認してから使う(分類は UploadMesh でしか行わない)。
    void RebindMaterialBaseTexture(uint32_t materialIndex, bool hasTexture,
                                   uint32_t texSlotOffset, bool hasNormal, bool hasOrm);

    // materialIndex の UV変換のみを書き換える(色/テクスチャ有無には触れない。
    // MaterialRuntime(ChangeMaterial/ChangeTexture)とAnimationRuntime(SetAnimation)
    // は独立に同じmaterialへ書き込みうるため、専用セッタで関心を分離する)。
    // uv6 = {a,b,c,d,tx,ty}（uv'=uv*[[a,c],[b,d]]+[tx,ty]）。既定=恒等変換。
    void UpdateMaterialUvTransform(uint32_t materialIndex, const float uv6[6]);

    const std::vector<MatData>& MaterialData() const { return transparency_.MatDataCpu(); }

    D3D12_CPU_DESCRIPTOR_HANDLE DepthDsv() const {
        return dsvHeap_ ? dsvHeap_->GetCPUDescriptorHandleForHeapStart()
                        : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DepthDsvReadOnly() const {
        if (!dsvHeap_) return D3D12_CPU_DESCRIPTOR_HANDLE{};
        D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += dsvDescriptorSize_;
        return handle;
    }
    bool HasMesh() const { return geometry_.HasMesh(); }

    // ジオメトリ側(頂点/インデックス/サブメッシュ/マテリアル/per-material マッピング
    // /半透明バッファ)だけを破棄する。テクスチャプール(texPool_)・ダミー table・
    // nextTexSlot_ は保持する。モデル切替(ChangeModel)は毎回 UploadMesh を呼ぶが、
    // テクスチャはバリアント間でほぼ不変なので、これらを保持して GPU テクスチャの
    // 再構築を丸ごと省く(切替時間の大半はテクスチャ再構築が占める)。
    void ResetGeometry() {
        geometry_.Reset();
        materialSet_.Clear();
        transparency_.Reset();
        // partWorldBuf_ は Init() で確保した Init 時常駐リソースなので破棄しない。
        // 差し替え後のメッシュはsubmesh構成が変わりうるため、cb枠切れ警告は
        // メッシュ単位で出し直せるようリセットする。
        cbOverflowWarned_ = false;
    }

    // GPUの完了を待たずにメッシュを差し替える前に呼ぶ。次の UploadMesh で解放されるもののうち、
    // 前フレームが使っている頂点/索引バッファと、そのフレーム枠の半透明バッファ・記述子を残す。
    // 残している間は同じキーの Acquire が旧ジオメトリを返すので、キーと内容が1対1の経路だけが使う
    // (固定キーを使う経路は、この関数を呼ばずにGPU完了を待ってから差し替える)。
    // currentFrameは次に記録するフレームの番号(Record*へ渡す値と同じ)。
    void BeginInFlightSwap(UINT currentFrame);
    // このパイプラインを捨てた後に同じモデルへ戻るときのために、共有ジオメトリを残す。
    void KeepGeometryForReuse() const { geometry_.KeepSharedForReuse(); }
    // 描画の記録前に毎フレーム呼ぶ。frameIndexの枠のGPU処理は終わっているので、残した資源を
    // 片づけ、その枠の半透明バッファを今のメッシュで作る。
    void AdvanceInFlightSwap(UINT frameIndex);
    // 書き換えモードのメッシュの頂点を、frameIndex の枠のバッファへ書く(MeshGeometry へ転送)。
    // 書き換えモードでない、または個数が合わないときは何もせず false。
    bool WriteDynamicVertices(UINT frameIndex, const MeshVertex* vertices, uint32_t count);

    // 永続テクスチャプールとダミー table を破棄し、スロット割当を先頭へ戻す。
    // プラグイン切替時(別プラグインのテクスチャで VRAM を専有し続けないよう)に
    // 呼ぶ。プール key は絶対解決済みパスのためプラグイン跨ぎの誤共有は無いが、
    // VRAM を有界化するために明示破棄する。
    void DiscardTexturePool() {
        texturePool_.DiscardTexturePool();
    }

    // テクスチャ枠の残り数。texturePool_ を保持したままジオメトリだけ差し替える
    // (同一プラグインのスイッチ切替)場合、テクスチャプール内のスロット割当は
    // 進む一方で戻らないため、呼び出し側はこれを見て「まだ保持したまま差し替えて
    // よいか / いったん DiscardTexturePool して枠を巻き戻すか」を判断する。
    UINT RemainingTextureSlots() const {
        return texturePool_.RemainingTextureSlots();
    }

    // texSlotOffset(パイプライン内相対スロット添字。baseの位置。+1=normal、
    // +2=ormがあれば続く)から、シェーダの bindless テーブルの配列が直接参照する
    // 絶対ヒープ添字を求める。texSlotOffset=0は常にダミー(白/平坦法線/既定ORM、
    // UploadMesh()で必ず先頭3スロットに常駐)を指す。
    uint32_t AbsoluteTexIndex(uint32_t texSlotOffset) const {
        return texturePool_.AbsoluteTexIndex(texSlotOffset);
    }

    // ジオメトリ＋テクスチャプール＋ダミーを全て破棄する(完全リセット)。
    void Reset() {
        ResetGeometry();
        DiscardTexturePool();
    }

private:
    // base + materialSet_ の materialIndex から MeshConstants を組み立て、フレーム f の
    // 定数バッファの cbSlot へ書き込み、その GPU 仮想アドレスを返す。RecordOpaque と
    // RecordTransparent が同一フレーム内で別スロットを使うために slot を明示する
    // （両者が同じスロットへ書くと、先に記録した Draw が参照する定数を後で上書き
    //   してしまう torn read になるため）。
    D3D12_GPU_VIRTUAL_ADDRESS WriteMeshConstants(const MeshConstants& base, UINT f,
                                                 UINT cbSlot, uint32_t materialIndex,
                                                 bool alphaTest);
    // WriteMeshConstants の書き込み先を選べる版(GPU仮想アドレスを返す)。通常の描画は constantBuffer_、
    // シャドウパスは shadowConstantBuffer_ へ書く。
    uint64_t WriteMeshConstantsTo(MeshConstantBuffer& target, const MeshConstants& base, UINT f,
                                  UINT cbSlot, uint32_t materialIndex, bool alphaTest,
                                  AlphaSplit alphaSplit = AlphaSplit::None);
    // Record*InstancedMultiPart共通のroot signature/VB/IB/記述子テーブル設定。
    // partsPerInstance等を埋めたMeshConstantsを返す。
    MeshConstants BindInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                         const MeshConstants& baseConstants, UINT f,
                                         UINT partsPerInstance);
    // submesh1個分の定数をconstantBufferのcbIndexのスロットへ書いて描く(スロット枯渇時は警告して飛ばす)。
    void DrawInstancedSubmesh(ID3D12GraphicsCommandList* cmdList, MeshConstants& base, UINT f,
                              const SubMesh& submesh, UINT instanceCount, UINT& cbIndex,
                              MeshConstantBuffer& constantBuffer);

    // シャドウパス用の定数バッファを、初回の使用時に作り、requiredSlots 枠まで育てる。失敗時 false。
    bool EnsureShadowConstantBuffer(UINT f, UINT requiredSlots);
    // 影パスで描いてよい材質か(材質ストアの現在の castShadow。実行時に変わる)。
    bool MaterialCastsShadow(const SubMesh& submesh) const;
    // 深度の面に沿って描く3種類のパス。submesh の振り分けと使う PSO の組が違う(MeshPipelineRecordPrepass.cpp)。
    // Velocity は深度ではなく速度を書くが、描く submesh と描き方はプリパスと同じ。
    enum class DepthOnlyPass { Shadow, MainPrepass, Velocity };
    ShadowCasterKind DepthOnlyKindOf(const SubMesh& submesh, DepthOnlyPass pass, bool includeThin) const;
    // pass の分類で submesh を深度だけで描く(Solid → AlphaTest の順)。cbIndex から shadowConstantBuffer_ のスロットを使う。
    void RecordDepthOnlyDraws(const rs::rhi::CommandList& commandList, MeshConstants& base, UINT f, DepthOnlyPass pass,
                              bool includeThin, UINT instanceCount, UINT& cbIndex);
    // DrawInstancedSubmesh が定数バッファの末尾に取っておく枠の数(通常の描画では半透明とハイライトの用)。
    static constexpr UINT kReservedTailSlots = 2;

    // 共有root signature/PSO一式への参照(所有はFrameRenderer側、MeshPipelineSharedState.h)。
    const MeshPipelineSharedState* shared_ = nullptr;
    ID3D12Device* device_ = nullptr;

    MeshGeometry geometry_;
    // UploadMesh()に渡されたgeometryKey(診断ログでの対象プラグイン特定用)。
    std::string geometryKey_;
    // cbオーバーフロー警告(DrawInstancedSubmesh参照)をこのパイプライン
    // インスタンス単位で1回だけ出すためのフラグ。関数ローカルstaticだと
    // プロセス内で最初に枠切れしたメッシュしか警告に出ず、2件目以降が
    // サイレントに抜ける。
    bool cbOverflowWarned_ = false;

    // フレームごとの定数バッファ（2 フレーム in flight。submesh 数分の領域。
    // 実体・容量拡張・解放待ちキューはMeshConstantBuffer.h参照)。
    static constexpr UINT kFrames = 2;
    // RecordInstancedMultiPartが使い終えた定数バッファスロットの次番号と、それを記録した
    // frameIndex(剰余前)。RecordLateOpaqueInstancedMultiPartはframeIndexが一致するときだけ
    // ここからスロットを割り当てる(不透明パスを経ていないフレームで古い値を使わないため)。
    static constexpr UINT kNoLateOpaqueFrame = ~0u;
    UINT lateOpaqueFrame_[kFrames] = {kNoLateOpaqueFrame, kNoLateOpaqueFrame};
    UINT blendedCbStart_[kFrames] = {0, 0};
    UINT lateOpaqueCbStart_[kFrames] = {0, 0};

    MeshMaterialSet materialSet_;
    MeshTransparency transparency_;

    // UploadMesh() の内部処理を責務ごとに分解したヘルパー群(MeshUpload.cppに実装)。

    // 全マテリアルの(base,normal,orm)をテクスチャプールへ登録/参照し、materialSet_を埋める。
    // profTexMsOutへ計測時間(ms)を書く(プロファイル出力用)。
    void UploadMaterialTextures(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                double& profTexMsOut);

    // submeshes_の各サブメッシュへalphaClassとalphaTestClipを設定する
    // (materialSet_を参照)。
    void ClassifySubmeshTransparency();

    // submeshes_各要素のcenterと、半透明サブメッシュのtransparentTris_を計算する。
    void ComputeSubmeshCentersAndTransparentTris(const Mesh& mesh);

    // 全マテリアルのMatData(CPU配列)を構築する。半透明の有無に関わらず必ず呼ぶ
    // (MaterialData()経由でTrainInstanceBatchの個体差オーバーライドが参照する)。
    void BuildMatDataCpu();

    // 半透明構造化バッファを準備する。失敗時 false。
    bool UploadTransparentBuffers();

    // 深度
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    UINT dsvDescriptorSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource>       depthBuffer_;
    DepthProbe                                   depthProbe_;

    // テクスチャ SRV 配置情報。srvHeap_/srvSize_ は深度バッファやgPartWorld等
    // 他用途のSRV書き込みでも使うため、テクスチャプール用と同じ値を Init() で
    // texturePool_ 側にも渡した上で、こちらにも保持する(値のコピー、共有はしない)。
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT                  srvSize_ = 0;

    // 永続テクスチャプール(MeshTexturePool.h参照)。UploadMesh(ベースメッシュ)と
    // PreloadTexture(ChangeTexture/SetAnimation候補)の両方がここへ登録・参照し、
    // モデル切替(ResetGeometry)を跨いで保持される。
    MeshTexturePool texturePool_;

    // gPartWorld/InstanceMatOverride/DynamicMaterialSlot(いずれもフレームごとの
    // UPLOADバッファ)の実体・容量拡張・解放待ちキュー管理(MeshDynamicBuffers.h参照)。
    MeshDynamicBuffers dynamicBuffers_;
    // submesh定数バッファ(cbUpload_系)の実体・容量拡張・解放待ちキュー管理
    // (MeshConstantBuffer.h参照)。
    MeshConstantBuffer constantBuffer_;
    // シャドウパス専用の定数バッファ(RecordShadowInstancedMultiPart の初回に作る)。
    MeshConstantBuffer shadowConstantBuffer_;
};

} // namespace rs::render

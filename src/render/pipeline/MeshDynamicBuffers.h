//
// MeshDynamicBuffers.h - MeshPipelineのgPartWorld/InstanceMatOverride/
// DynamicMaterialSlot(いずれもフレームごとのUPLOADバッファ)管理を切り出したクラス。
//
// インスタンシング向けにMeshPipelineへ追加された3種のフレーム別
// UPLOADバッファ群は、実体と容量拡張を一緒に管理する。
// MeshPipelineRecord.cppからは読み取りアクセサでしか参照されない(定数バッファ
// cbUpload_等と異なりRecord側の書き込みが無い)ため、この境界で管理する。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "MeshPipelineTypes.h"
#include "PartWorldHistory.h"

namespace rs::render {

class MeshDynamicBuffers {
public:
    static constexpr UINT kFrames = 2;

    // srvHeap は shader-visible。firstPartWorldDescriptorIndex/
    // firstInstanceMatOverrideDescriptorIndex 以降の descriptor をこのクラスが使う。
    bool Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
              UINT firstPartWorldDescriptorIndex, UINT firstInstanceMatOverrideDescriptorIndex,
              UINT maxParts);

    // パーツ単位ワールド行列を gPartWorld へ書き込む。空なら恒等行列1個を
    // partIndex=0 として書く（単一メッシュ表示との後方互換）。
    // RecordInstanced 用途では xforms[i] が instanceId=i のワールド行列になる。
    // continuityKey は、xforms の並びが前回書いたものと対応しているかを決めるキー
    // (PartWorldHistory 参照)。前回と同じキーが続く間は、前回の行列を prev として使う。
    void UpdatePartWorlds(const std::vector<PartXform>& xforms, UINT frameIndex,
                          uint64_t continuityKey = 0);
    bool EnsureInstanceMatOverrideCapacity(ID3D12Device* device, UINT requiredOverrides,
                                           UINT frameIndex);
    void UpdateInstanceMatOverrides(const std::vector<InstanceMatOverride>& overrides,
                                    UINT frameIndex);
    void ConfigureDynamicMaterialSlots(const std::vector<uint32_t>& materialIndices,
                                       size_t materialCount);

    // gPartWorld バッファを requiredParts 個以上へ拡張する(frameIndex のフレーム分
    // のみ)。既に足りていれば何もしない。拡張時は新バッファを作り直して SRV を
    // 書き換えるため、「そのフレーム番号のGPU実行が完了済み」であること(=通常の
    // フレーム開始後)が前提。UpdatePartWorlds の直前に呼ぶこと。
    // インスタンス描画で配置数が初期容量を超えても取りこぼさないためのもので、
    // 逆に初期容量を小さく取れるので大量バッチ時のUPLOADヒープ消費も下がる。
    bool EnsurePartWorldCapacity(ID3D12Device* device, UINT requiredParts, UINT frameIndex);

    void BindInstanceMatOverrideTable(ID3D12GraphicsCommandList* cmdList, UINT f) const;

    void SetRetirementQueue(rs::rhi::RetirementQueue* queue) { retirementQueue_ = queue; }

    // --- Record* 向けの読み取りアクセサ ---
    // フレーム f の gPartWorld にある、前フレーム分の行列の先頭の要素番号。0 なら前フレーム分は無い
    // (動いているパーツが無い)。シェーダは「今の行列の、この要素数あと」を前フレームの行列として読む。
    // 前フレーム分は、動いているパーツがある回に書いた枠だけにある(初めて動いたとき、その枠だけ広げる)。
    UINT PrevPartWorldOffset(UINT f) const { return partWorldPrevOffset_[f % kFrames]; }
    UINT FirstPartWorldIndex() const { return firstPartWorldIndex_; }
    UINT FirstInstanceMatOverrideIndex() const { return firstInstanceMatOverrideIndex_; }
    // マテリアル番号→動的スロット番号のルックアップテーブル(要素数=メッシュの
    // 全マテリアル数M)。gMaterialToDynamicSlot[materialIndex]の添字範囲はこの
    // 配列自体のSRVサイズで決まるため、Mを別途シェーダへ渡す必要はない。
    const std::vector<uint32_t>& DynamicMaterialSlots() const { return dynamicMaterialSlots_; }
    // 1インスタンスあたりの動的マテリアルの実数D(=ConfigureDynamicMaterialSlots
    // に渡されたmaterialIndicesの要素数)。gInstanceMatOverrides側のper-instance
    // ストライドはこちら(D)であり、上のDynamicMaterialSlots().size()(=M、
    // D<=Mでほぼ常にD<M)とは別物。取り違えるとinstanceId>=1の全読み出しが
    // ずれる。
    UINT DynamicMaterialSlotCount() const { return dynamicMaterialSlotCount_; }
    bool HasInstanceMatOverrides() const { return hasInstanceMatOverrides_; }
    const std::vector<PartXform>& PartXforms() const { return history_.Curr(); }
    // 前回の UpdatePartWorlds で渡した行列(対応が崩れたときは今回と同じ値)。
    const std::vector<PartXform>& PrevPartXforms() const { return history_.Prev(); }
    // 直近の UpdatePartWorlds で、前回とワールド行列が1つでも違ったか。
    bool PartWorldsMoving() const { return history_.Moving(); }

private:
    // フレーム f の gPartWorld バッファ(UPLOADヒープ)を parts 個ぶん作り直し、
    // Map して SRV(firstPartWorldIndex_ + f)を張り替える。Init() と
    // EnsurePartWorldCapacity() の共通経路。
    // withPrev が真なら、前フレーム分の行列(今の行列と同じ要素数)を後ろに並べるぶん、バッファを2倍にする。
    bool CreatePartWorldBuffer(UINT f, UINT parts, bool withPrev = false);
    bool CreateInstanceMatOverrideBuffers(ID3D12Device* device, UINT f, UINT count);
    bool CreateDynamicMaterialSlotBuffer(UINT f);

    ID3D12Device*          device_ = nullptr;
    // テクスチャ SRV 配置情報。srvHeap_/srvSize_ は他用途のSRV書き込みでも使うため、
    // MeshPipeline側と同じ値をInit()で受け取り、こちらにも保持する(値のコピー、
    // 共有はしない。MeshTexturePoolと同じ流儀)。
    ID3D12DescriptorHeap*  srvHeap_ = nullptr;
    UINT                   srvSize_ = 0;

    // パーツ単位ワールド行列（gPartWorld, t0 space3）。フレームごとに1枚の
    // UPLOAD バッファを持ち、UpdatePartWorlds() で内容だけ書き換える。容量は
    // EnsurePartWorldCapacity() でフレーム単位に拡張されうる。
    Microsoft::WRL::ComPtr<ID3D12Resource> partWorldBuf_[kFrames];
    uint8_t*                               partWorldMapped_[kFrames] = {};
    UINT                                   partWorldCapacity_[kFrames] = {};
    bool                                   partWorldHasPrev_[kFrames] = {};  // バッファが前フレーム分の領域を持つか
    UINT                                   partWorldPrevOffset_[kFrames] = {}; // 直近に書いた内容の前フレーム分の先頭(0=なし)
    UINT                                   firstPartWorldIndex_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceMatOverrideBuf_[kFrames];
    InstanceMatOverride*                    instanceMatOverrideMapped_[kFrames] = {};
    UINT                                    instanceMatOverrideCapacity_[kFrames] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> dynamicMaterialSlotBuf_[kFrames];
    uint32_t*                               dynamicMaterialSlotMapped_[kFrames] = {};
    UINT                                    firstInstanceMatOverrideIndex_ = 0;
    std::vector<uint32_t>                   dynamicMaterialSlots_;
    UINT                                     dynamicMaterialSlotCount_ = 0;
    bool                                    hasInstanceMatOverrides_ = false;
    rs::rhi::RetirementQueue* retirementQueue_ = nullptr;
    PartWorldHistory                       history_; // 半透明ソート用にCPU側にも保持。前回分との対応も持つ
};

} // namespace rs::render

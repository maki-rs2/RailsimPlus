//
// MeshPipelineTypes.h - MeshPipeline/MeshTexturePool 双方が共有する型定義。
//
// MeshTexturePool.h が MeshPipeline.h をincludeせずに InstanceMatOverride 等の
// 型を使えるよう、共通のCBV/SRVレイアウト型をこの独立ヘッダへ切り出す
// (MeshPipeline.h は本ヘッダを include し、MeshTexturePool.h も同様に include
// する片方向依存にすることで、相互includeを避ける)。
//
#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "../mesh/Mesh.h"
#include "MeshConstants.h"
#include "EnvFlag.h"
#include "SubmeshAlphaClassify.h"
#include "../sky/AtmosphereMath.h"
#include "../rhi/RetirementQueue.h"

namespace rs::render {

// 汎用のUPLOADバッファ(UPLOAD・GENERIC_READ・ROW_MAJOR、中身は0初期化)を作る。
// 64KB以下は共有ヒープからの配置リソース、それより大きいものはcommittedで作る
// (実体は src/render/resource/UploadBufferPool.cpp)。失敗時はoutに触れずfalse。
bool CreatePooledUploadBuffer(ID3D12Device* device, UINT64 byteSize,
                              Microsoft::WRL::ComPtr<ID3D12Resource>& out);

// 非同期の描画では共通フェンスまで保持する。キューを持たない撮影経路は差し替え前にGPUを待つ。
template <typename Buffer>
void RetireBuffer(rs::rhi::RetirementQueue* queue, Buffer& oldBuf) {
    if (queue && oldBuf)
        queue->Retire(std::make_shared<Buffer>(std::move(oldBuf)));
    else
        oldBuf.Reset();
}

// Weighted Blended OIT の蓄積バッファ形式（WboitPass と半透明 PSO で共有）。
//   accum  = Σ(color*a, a)*w を保持（HDR 加算のため 16bit float）
//   reveal = Π(1-a) を保持（0..1 のスカラー）
constexpr DXGI_FORMAT kWboitAccumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kWboitRevealFormat = DXGI_FORMAT_R16_FLOAT;

// Reversed-Z の深度バッファのクリア値(遠方=0)。深度バッファの生成とクリアで共有する。
constexpr float kReversedZClearDepth = 0.0f;

// GPU 上に載った1マテリアル分のテクスチャ（base/normal/orm の3枚）。
// GPUへの絶対添字は texSlotOffset から AbsoluteTexIndex() で都度計算する
// (Bindless化によりCPU側でGPUハンドルを事前計算・保持する必要がなくなった)。
struct GpuTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;   // base
    Microsoft::WRL::ComPtr<ID3D12Resource> normalRes;
    Microsoft::WRL::ComPtr<ID3D12Resource> ormRes;
    bool                                   valid = false;   // base あり
    bool                                   hasNormal = false;
    bool                                   hasOrm = false;
    bool                                   hasAlpha = false;       // base に alpha<255 の texel あり
    bool                                   hasZeroAlpha = false;   // base に alpha==0 texel あり → 穴クリップ(cutout)対象
    float                                  semiAlphaFraction = 0.0f; // base のalpha>0のtexelのうち半透明の割合
    // firstTexIndex_ を起点に、このマテリアルの base が何スロット目か(パイプライン内
    // 相対添字)。半透明パスの PS が gTexArr[texSlotOffset+n] でサンプルするための添字。
    // テクスチャ無しは 0（ダミー白/平坦法線/既定ORM）。
    uint32_t                               texSlotOffset = 0;
};

// 半透明パスの PS がマテリアル選択に使う定数。HLSL の MatData と float4 単位で
// レイアウトを厳密一致させること（StructuredBuffer<MatData> gMats に対応）。
struct MatData {
    float    diffuse[4]  = {0.8f, 0.8f, 0.8f, 1.0f}; // rgb=拡散色, a=材質アルファ
    float    ambient[4]  = {0.8f, 0.8f, 0.8f, 0.0f}; // rgb=環境光色
    float    emissive[4] = {0, 0, 0, 0};             // rgb=emissive
    float    flags[4]    = {0, 0, 0, 0};             // x=useNormal y=useOrm z=useEnvMap(EnvMap指定の材質) w=影を受けない(1)
    uint32_t tex[4]      = {0, 0, 0, 0};             // x=絶対ヒープ添字(AbsoluteTexIndex) y=useBase zw未使用
    float    specular[4] = {0, 0, 0, 0};             // xyz=Specular色, w=Power
    // SetAnimation(UV型アニメ): uv'=uv*[[a,c],[b,d]]+[tx,ty]。既定=恒等変換。
    float    uvTransform0[4] = {1, 0, 0, 1};         // a,b,c,d
    float    uvTransform1[4] = {0, 0, 0, 0};         // tx,ty,z=点サンプリング(1),0
};

// HLSL の MatData(MeshShader.h)と同じ並び・大きさであることを確かめる。
static_assert(sizeof(MatData) == size_t{8} * 16, "MatData must be 8 float4 to match HLSL");
static_assert(offsetof(MatData, tex) == size_t{4} * 16 && offsetof(MatData, uvTransform1) == size_t{7} * 16,
              "MatData field order must match HLSL");

// Per-instance resolved material data.  Kept identical to the transparent-path
// material layout so texture, UV animation, and ChangeMaterial fields survive.
using InstanceMatOverride = MatData;
constexpr uint32_t kInvalidDynamicMaterialSlot = 0xffffffffu;

// Material(ChangeMaterial適用後の色情報)の色フィールド(diffuse/emissive/
// specular+power)をMatData(InstanceMatOverrideと同一型)へ書き写す。
// 加えて EnvMap 指定の有無(flags[2])、影を受けない印(flags[3])、テクスチャを点サンプリングで読む印
// (uvTransform1[2]。AlphaZeroTest)も書く(条件ごとに実行時に変わるため色と一緒に運ぶ)。
// 法線/ORMの使用(flags[0]/[1])とテクスチャ・UV変換フィールド(uvTransform1[2]を除く)は対象外(呼び出し側が別途書き込む)。フォールバック
// 経路(MeshMaterialRuntime.cpp::UpdateMaterialColor)とバッチ経路
// (SceneMaterialOverrides.cpp::ResolveSceneStaticMaterialOverrides)の両方が
// 同じ変換を別々に書いていたため、共通ヘルパーへ集約した。
inline void ApplyMaterialColorToMatData(const Material& src, MatData& dst) {
    dst.diffuse[0] = src.diffuse[0];
    dst.diffuse[1] = src.diffuse[1];
    dst.diffuse[2] = src.diffuse[2];
    dst.diffuse[3] = src.diffuse[3];
    dst.ambient[0] = src.ambient[0];
    dst.ambient[1] = src.ambient[1];
    dst.ambient[2] = src.ambient[2];
    dst.emissive[0] = src.emissive[0];
    dst.emissive[1] = src.emissive[1];
    dst.emissive[2] = src.emissive[2];
    dst.specular[0] = src.specular[0];
    dst.specular[1] = src.specular[1];
    dst.specular[2] = src.specular[2];
    dst.specular[3] = src.power;
    dst.flags[2] = src.useEnvMap ? 1.0f : 0.0f;
    dst.flags[3] = src.receiveShadow ? 0.0f : 1.0f;
    dst.uvTransform1[2] = AlphaZeroUsesPointSampling(src.forceAlphaTest, AlphaZeroKBuildEnabled()) ? 1.0f : 0.0f;
}

// 初期アップロード時に Material のUV変換を含む描画パラメータを MatData へ転記する。
// 色だけを更新する経路では ApplyMaterialColorToMatData を使い、進行中のUVアニメーションを維持する。
inline void ApplyMaterialColorAndUvTransformToMatData(const Material& src, MatData& dst) {
    ApplyMaterialColorToMatData(src, dst);
    dst.uvTransform0[0] = src.uvTransform[0];
    dst.uvTransform0[1] = src.uvTransform[1];
    dst.uvTransform0[2] = src.uvTransform[2];
    dst.uvTransform0[3] = src.uvTransform[3];
    dst.uvTransform1[0] = src.uvTransform[4];
    dst.uvTransform1[1] = src.uvTransform[5];
}

// Material の色・ライティング・UV変換を MeshConstants へ転記する。
inline void ApplyMaterialColorToMeshConstants(const Material& src, MeshConstants& dst) {
    dst.diffuse[0] = src.diffuse[0];
    dst.diffuse[1] = src.diffuse[1];
    dst.diffuse[2] = src.diffuse[2];
    dst.diffuse[3] = src.diffuse[3];
    dst.ambient[0] = src.ambient[0];
    dst.ambient[1] = src.ambient[1];
    dst.ambient[2] = src.ambient[2];
    dst.emissive[0] = src.emissive[0];
    dst.emissive[1] = src.emissive[1];
    dst.emissive[2] = src.emissive[2];
    dst.specular[0] = src.specular[0];
    dst.specular[1] = src.specular[1];
    dst.specular[2] = src.specular[2];
    dst.specular[3] = src.power;
    dst.uvTransform0[0] = src.uvTransform[0];
    dst.uvTransform0[1] = src.uvTransform[1];
    dst.uvTransform0[2] = src.uvTransform[2];
    dst.uvTransform0[3] = src.uvTransform[3];
    dst.uvTransform1[0] = src.uvTransform[4];
    dst.uvTransform1[1] = src.uvTransform[5];
}

// 半透明描画用の三角形単位エントリ（サブメッシュ内の3インデックス単位）
struct TransparentTri {
    uint32_t materialIndex = 0;
    uint32_t idx[3] = {0, 0, 0}; // 三角形を構成する3頂点インデックス
    float    center[3] = {0, 0, 0}; // ローカル空間の三角形重心
    uint32_t partIndex = 0; // gPartWorld 添字（距離ソート時のワールド変換に使用）
};

// 1パーツ分のワールド変換。頂点シェーダが partIndex で gPartWorld から引く。
// HLSL の PartXform と float4x4 単位でレイアウト一致させること。
struct PartXform {
    float world[16];
    float normalMat[16];
};

// PreloadTexture/FindPreloadedSlot の出力。hasNormal/hasOrm は必ずプール側の
// MeshTexturePool::PooledTexGroup(=実際に確保・アップロードされたリソースの
// 有無)から導出する。プールが basePath 単体をキーにしているため、呼び出し側が
// パス文字列の空/非空から独自に判定すると、同一 base が別の normal/orm 有無で
// 再要求された際に隣接スロットを誤って法線/ORMとしてサンプルする事故になりうる。
struct PreloadedTexInfo {
    uint32_t texSlotOffset = 0;
    bool     hasNormal = false;
    bool     hasOrm = false;
    // base のαの性質(読込済みテクスチャから取った値)。UploadMesh の透明分類
    // (ClassifySubmeshTransparency)が見る入力なので、メッシュを上げ直さずに
    // テクスチャだけ差し替えてよいか(分類が変わらないか)の判定に使う。
    bool     hasAlpha = false;
    bool     hasZeroAlpha = false;
    float    semiAlphaFraction = 0.0f;
};

// base/normal/pbr の3つ組をプールへ登録するときに使うテクスチャスロット数。
// 法線/ORM のどちらかがあれば base+normal+orm の3スロット、無ければ base の1スロット。
// MeshTexturePool の枠確保と、事前ロード対象の予算計算(PreloadTextureSet)が同じ値を使う。
inline uint32_t TextureSlotCost(const std::string& normalPath, const std::string& pbrPath) {
    return (normalPath.empty() && pbrPath.empty()) ? 1u : 3u;
}

} // namespace rs::render

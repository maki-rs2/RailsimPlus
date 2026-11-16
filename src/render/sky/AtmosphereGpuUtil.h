//
// AtmosphereGpuUtil.h - 大気LUT生成(AtmosphereLuts・AerialPerspectiveLut)と空パス(SkyPass)が共有するDX12の下ごしらえ
//
// ルート署名・シェーダのコンパイル・フルスクリーン三角形のPSO・LUTへの全面描画。DX12型を使うので、
// これらの.cppからだけ include する内部用ヘッダ(公開ヘッダには出さない)。
//
#pragma once

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../postprocess/SceneHdrTarget.h"

namespace rs::render {

// SRVテーブル(srvCount>0なら0番、t0から連続)+ルート定数(constantDwords>0なら次、b0)+
// 追加のSRVテーブル(extraSrvCount>0なら最後。レジスタは t(srvCount) から連続。先のテーブルと
// 別の場所にあるSRVを引く用)+静的サンプラ(s0=リニアクランプ)のルート署名。
// allowInputLayout=true は、頂点バッファを使う描画(インプットレイアウトあり)のとき。頂点シェーダも
// ルート定数を読めるよう、ルート定数の可視性を全ステージにする。
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateAtmosphereRootSignature(ID3D12Device* device,
                                                                          UINT srvCount,
                                                                          UINT constantDwords,
                                                                          UINT extraSrvCount = 0,
                                                                          bool allowInputLayout = false);

// 上と同じ並び(SRVテーブル→ルート定数→追加のSRVテーブル群)で、追加のSRVテーブルを複数持てる版。
// extraTableSrvCounts の要素ごとに1つのテーブルを、先の追加テーブルの直後のレジスタから連続して割り当てる
// (別々の場所にあるSRVを何組も引く用)。ルートの番号は、SRVテーブル・ルート定数があればそれぞれ1つ使ったあとの順。
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateAtmosphereRootSignatureTables(
    ID3D12Device* device, UINT srvCount, UINT constantDwords, const std::vector<UINT>& extraTableSrvCounts,
    bool allowInputLayout = false);

// 共通のシェーダ文字列をコンパイルする。useMultiScat は HLSL の RS_USE_MULTISCAT の値。
Microsoft::WRL::ComPtr<ID3DBlob> CompileAtmosphereShader(const std::string& source, const char* entry,
                                                         const char* target, bool useMultiScat);

// 深度なし・ブレンドなし・カリングなしの、SV_VertexIDで描く全画面三角形のPSO。
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateAtmosphereFullscreenPso(ID3D12Device* device,
                                                                          ID3D12RootSignature* rootSig,
                                                                          ID3DBlob* vs, ID3DBlob* ps,
                                                                          DXGI_FORMAT rtvFormat);

// target へフルスクリーン三角形を描く(全面を描くのでクリアはしない)。tableGpuHandle が0以外なら、
// ルート0のSRVテーブルとしてバインドする。constantDwords が0より大きければ、次のルートにルート定数を積む。
// 終わったら target をSRV状態へ戻す。呼び出し側が先にSRVヒープを設定しておくこと。
// rootSigNative・psoNative はバックエンドのルート署名・PSOへのポインタ(各バックエンドの型)。
void DrawAtmosphereLut(const rs::rhi::CommandList& commandList, SceneHdrTarget& target, void* rootSigNative,
                       void* psoNative, std::uint64_t tableGpuHandle, const void* constants,
                       std::uint32_t constantDwords);

} // namespace rs::render

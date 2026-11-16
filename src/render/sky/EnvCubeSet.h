//
// EnvCubeSet.h - Envプラグインの焼き済みキューブ一式(拡散・鏡面キューブとBRDF LUT)のGPU資源
//
// メモリ上へ読み済みの画像(EnvTextureLoader)をGPUテクスチャにして、共有SRVヒープの連続3枠へSRVを書く。
// 3枠の並びは先頭から 拡散・鏡面・BRDF LUT。空は大気の計算で描くので、EnvMapTexFileNameの背景キューブは読まない。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../resource/EnvTextureLoader.h"
#include "../rhi/CommandQueue.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

struct EnvCubeImages {
    EnvTextureData diffuse;
    EnvTextureData specular;
    EnvTextureData brdfLut;
};

class EnvCubeSet {
public:
    static constexpr std::uint32_t kSrvCount = 3;  // 先頭から 拡散・鏡面・BRDF LUT

    EnvCubeSet();
    ~EnvCubeSet();
    EnvCubeSet(const EnvCubeSet&) = delete;
    EnvCubeSet& operator=(const EnvCubeSet&) = delete;

    // device・srvHeap・queue は使い終わるまで生きていること。firstSrvIndex から kSrvCount 枠を使う。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, const rs::rhi::CommandQueue& queue,
              std::uint32_t firstSrvIndex);

    // images を検証し(拡散・鏡面はキューブ、BRDF LUTは2D、すべてok)、GPUテクスチャを作ってSRVを書く。
    // アップロードの完了まで待つ。旧テクスチャは置き換えるので、呼び出し側が事前にGPUの完了を待つこと。
    // 失敗したら旧資源も捨てて Ready()=false にし、false を返す(理由は LastError())。
    bool Upload(const EnvCubeImages& images);

    // 資源を捨てて Ready()=false にする(呼び出し側が事前にGPUの完了を待つこと)。
    void Clear();

    bool Ready() const;
    std::uint32_t SpecularMipCount() const;

    std::uint32_t DiffuseSrvIndex() const;
    std::uint32_t SpecularSrvIndex() const;
    std::uint32_t BrdfLutSrvIndex() const;

    // 鏡面キューブのSRVのGPUハンドル(IBLの焼き込みが写真の空の層として1枠のテーブルでバインドする)。未初期化なら0。
    std::uint64_t SpecularSrvGpuHandle() const;

    // 中身が入れ替わるたびに増える番号(Upload の成功・Clear)。Env の切り替えで同じSRV枠の中身が
    // 変わったことを、描画側(IBLの再生成判定)が知るために使う。未初期化なら0。
    std::uint64_t ContentSerial() const;

    // 直近の Upload の失敗理由(成功・未実行なら空)。
    const char* LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

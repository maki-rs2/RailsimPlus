//
// ExposureTarget.h - 暗順応の結果(1x1のテクスチャ)の描画先と、そのSRV
//
// 幅2×高さ1の2画素。x=露出へ掛ける倍率 gain、y=順応の照度 E。
//   画素0(今): IblGenerator が毎フレーム描く。空パス・直射光・環境ソースの焼き込みが読む。
//   画素1(焼き込み時): IBLのキューブを作り直したフレームだけ描く。メッシュのシェーダが、キューブが古いぶんを
//   (画素0の E×gain)/(画素1の E×gain) の比で補正するために読む。
// SRVを Texture2D<float4> として Load で読む(CPUへは読み戻さない)。形式は精度のためR32G32B32A32_FLOAT。
// SRVは共有SRVヒープの1枠。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../postprocess/SceneHdrTarget.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class ExposureTarget {
public:
    ExposureTarget();
    ~ExposureTarget();
    ExposureTarget(const ExposureTarget&) = delete;
    ExposureTarget& operator=(const ExposureTarget&) = delete;

    // 資源・RTV・SRVを作る。srvIndex は srvHeap の何番目の枠をSRVに使うか。生成直後はSRV状態。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex);

    bool Ready() const;

    // 必要なら状態遷移のバリアを記録する(同じ状態なら何もしない)。
    void Transition(const rs::rhi::CommandList& commandList, HdrTargetState state);

    // 画素の数(0=今、1=IBL焼き込み時)。
    static constexpr std::uint32_t kPixelCount = 2;
    static constexpr std::uint32_t kCurrentPixel = 0;
    static constexpr std::uint32_t kBakedPixel = 1;

    // RTVにバインドし、ビューポートとシザーを指定の1画素だけにする(クリアはしない)。RenderTarget状態にしてから呼ぶこと。
    void Bind(const rs::rhi::CommandList& commandList, std::uint32_t pixel) const;

    std::uint64_t SrvGpuHandle() const;

    // 開発用の確認(環境変数 RS_DEBUG_ADAPTATION=1 のときだけ働く。起動後の最初の呼び出しで1回だけ読む)。
    // RecordReadback は SRV状態の資源の中身を読み戻し用バッファへ写す記録、PollReadback は数フレーム後に
    // 結果(画素0の倍率と順応の照度)を取り出して true を返す(1回の Record につき1回)。ゲームの描画経路は読み戻しに依存しない。
    static bool DebugReadbackEnabled();
    void RecordReadback(const rs::rhi::CommandList& commandList);
    bool PollReadback(float& gain, float& luminance);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

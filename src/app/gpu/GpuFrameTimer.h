//
// GpuFrameTimer.h - DX12タイムスタンプクエリによるパス別GPU実時間計測
//
// vsync予算超過フレームの原因がGPU側のどのパスにあるかを切り分けるための
// 診断クラス(性能調査用、docs方針: CPU実作業/Present/フェンス待ちだけでは
// 「GPUで何が重いか」が分からないため導入)。計測はGPUのタイムスタンプクエリ
// (D3D12_QUERY_TYPE_TIMESTAMP)を使い、結果はREADBACKヒープ経由でCPUへ読み戻す。
// 初期化に失敗した環境でも本体の描画に影響しないよう、Init失敗後は全メソッドが
// 何もしない(問い合わせは常に0を返す)。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

namespace rs::app {

using Microsoft::WRL::ComPtr;

// 計測区間の意味(FrameRenderer::Render/App::Render側で使う)。
constexpr size_t kGpuRegionFrame = 0;     // フレーム全体
constexpr size_t kGpuRegionOpaque = 1;    // 不透明パス
constexpr size_t kGpuRegionTransparent = 2; // WBOIT透明パス(記録)
constexpr size_t kGpuRegionComposite = 3; // WBOIT合成
constexpr size_t kGpuRegionImgui = 4;     // ImGui
constexpr size_t kGpuRegionOverlay2D = 5; // 2D overlay
constexpr size_t kGpuRegionTonemap = 6;   // トーンマップ(HDR→バックバッファ)
constexpr size_t kGpuRegionAtmosphere = 7; // 大気LUTの更新(太陽が動いたフレームだけ重い)
constexpr size_t kGpuRegionIbl = 8;       // IBLの生成(太陽が1度以上動いたフレームだけ重い)
constexpr size_t kGpuRegionShadow = 9;    // シャドウパス(影を落とすものの深度描画。夜・影なしの間は0)
constexpr size_t kGpuRegionBloom = 10;     // ブルーム(縮小チェーンと足し戻し。掛けない間は0)
constexpr size_t kGpuRegionFxaa = 11;      // FXAA(トーンマップ後のLDR→バックバッファ。掛けない間は0)
constexpr size_t kGpuRegionPrepass = 12;   // 主視点の深度プリパス(アンビエントオクルージョンが有効な間だけ。掛けない間は0)
constexpr size_t kGpuRegionAo = 13;        // アンビエントオクルージョン(GTAOとぼかし。掛けない間は0)
constexpr size_t kGpuRegionMotionBlur = 14; // モーションブラー(速度の合成・タイル・ぼかし。掛けない間は0)
constexpr size_t kGpuRegionVolumetricFog = 15; // ボリュメトリックフォグの格子(注入と積分。使わない間は0)
constexpr size_t kGpuRegionVelocity = 16;  // 速度パス(プリパスの深度に沿って動くパーツの速度を書く。使わない間は0)
constexpr size_t kGpuRegionDepthOfField = 17; // フォトモードの被写界深度(プリフィルタ・タイル・ギャザー・合成。掛けない間は0)
constexpr size_t kMaxGpuRegions = 18;

class GpuFrameTimer {
public:
    // クエリヒープ(D3D12_QUERY_HEAP_TYPE_TIMESTAMP)とREADBACKバッファを作る。
    // ringDepth はGpuContextのフレームバッファ数(kFrameCount)と同じ値を渡すこと。
    // 失敗時はfalseを返し、以降このインスタンスの全メソッドは何もしない。
    bool Init(ID3D12Device* device, ID3D12CommandQueue* queue, UINT ringDepth);

    // このフレームで使うリングスロットを設定する。呼び出し側は、そのスロットの
    // GPU処理完了をフェンスで待った直後に呼ぶこと(前回このスロットへ書かれた
    // クエリ結果をREADBACKバッファから問題なく読み出せるタイミングのため)。
    void BeginFrame(UINT ringIndex);

    // regionIndexの開始/終了タイムスタンプをコマンドリストへ記録する。
    void Begin(ID3D12GraphicsCommandList* cmdList, size_t regionIndex);
    void End(ID3D12GraphicsCommandList* cmdList, size_t regionIndex);

    // 現在のリングスロット分の全クエリをREADBACKバッファへ書き出す。
    // コマンドリストのClose前に呼ぶこと。
    void Resolve(ID3D12GraphicsCommandList* cmdList);

    // BeginFrame()で読み出した直近の結果(ms)。未計測の区間は0を返す。
    double LastMs(size_t regionIndex) const;

private:
    bool valid_ = false;
    UINT ringDepth_ = 0;
    UINT64 timestampFrequency_ = 0;
    UINT currentRing_ = 0;

    ComPtr<ID3D12QueryHeap> queryHeap_;
    ComPtr<ID3D12Resource> readbackBuffer_;
    const UINT64* mappedReadback_ = nullptr; // Init時にpersistent mapする

    // リングスロットごとに、直近そのスロットへ書き込まれた際にEndまで記録
    // された区間はどれか(BeginFrame()で読み出し可否の判定に使う)。
    std::vector<std::array<bool, kMaxGpuRegions>> recordedByRing_;
    // BeginFrame()で読み出した直近の結果(ms)。
    std::array<double, kMaxGpuRegions> lastMs_{};
};

} // namespace rs::app

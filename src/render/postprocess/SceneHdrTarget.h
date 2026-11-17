//
// SceneHdrTarget.h - シーン描画先のHDR中間レンダーターゲット(R16G16B16A16_FLOAT)
//
// 空・不透明・LateOpaque・ハイライト・WBOITコンポジットはバックバッファではなくここへ描き、
// 最後にTonemapPassでバックバッファへ変換する。
// 解像度は描画先(バックバッファ/撮影解像度)と同じで、リサイズ時はResizeで作り直す。
// RTVは自前の1枠ヒープ、SRVは呼び出し側が渡すシェーダ可視ヒープの1枠を使う。
// 固定サイズの入れ物としても使う(大気LUTの3枚。render/sky/AtmosphereLuts)。
// 状態(SRV/RT/コピー先)は内部で追跡し、Transitionは必要な遷移だけを記録する。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"


namespace rs::render {

enum class HdrTargetState {
    ShaderResource, // トーンマップが読む状態(生成直後・描画後)
    RenderTarget,   // 描画中
    CopyDest,       // バッファからの背景コピー先(サムネイル用)
    CopySource,     // 別の資源へのコピー元(半透明合成が背景を読むため)
};

// 資源の精度。既定の Half16 は R16G16B16A16_FLOAT(シーンの描画先)、Float32 は R32G32B32A32_FLOAT。
// 大気のSky-View LUTのように、薄明の暗い値(fp16の最小の非正規数に近い)を段にしたくない入れ物は Float32。
// Srgb8 は R8G8B8A8_UNORM_SRGB(トーンマップ後のLDRを置く入れ物。FXAAの入力)。サンプルするとリニア値で返る。
// Unorm8 は R8_UNORM(アンビエントオクルージョンの結果を置く入れ物。赤だけ使う)。
// Rg16Float は R16G16_FLOAT(速度バッファ。動く物の NDC の差を xy に置く)。
// R32Float は R32_FLOAT(測光パスの結果。1画素に1つの値を置き、CPU へ読み戻す)。
enum class HdrTargetFormat {
    Half16,
    Float32,
    Srgb8,
    Unorm8,
    Rg16Float,
    R32Float,
};

class SceneHdrTarget {
public:
    SceneHdrTarget();
    ~SceneHdrTarget();
    SceneHdrTarget(const SceneHdrTarget&) = delete;
    SceneHdrTarget& operator=(const SceneHdrTarget&) = delete;

    // device・srvHeap は使い終わるまで生きていること(所有しない)。srvIndex は srvHeap の
    // 何番目の枠をこのターゲットのSRVに使うか。clearColorLinear はBeginSceneのクリアに使う
    // リニアRGB(資源の最適化クリア値にもなる)。資源はResizeで作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex,
              const float clearColorLinear[3], HdrTargetFormat format = HdrTargetFormat::Half16);

    // 資源・RTV・SRVを(再)生成する。呼ぶ前にGPUがこの資源を使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    // 消去色を変える。消去色は資源の最適化クリア値でもあるので、色が変わったときは今の大きさで
    // 資源を作り直す(同じ色なら何もしない)。呼ぶ前にGPUがこの資源を使い終えていること。
    bool SetClearColor(const float clearColorLinear[3]);

    bool Ready() const;
    std::uint32_t Width() const;
    std::uint32_t Height() const;

    // 必要なら状態遷移のバリアを記録する(同じ状態なら何もしない)。
    void Transition(const rs::rhi::CommandList& commandList, HdrTargetState state);

    // 追跡している状態だけを書き換える(バリアは記録しない)。記録したコマンドを実行せずに捨てたとき、
    // 実際の状態へ追跡を戻すために使う。
    void ForceState(HdrTargetState state);

    // RenderTarget状態へ遷移し、depthDsv(無効なら深度なし)と合わせてバインドし、
    // clearColorLinear でクリアする。
    void BeginScene(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor depthDsv);

    rs::rhi::CpuDescriptor Rtv() const;

    // 自分の SRV の、ヒープ内の番号。ピクセルシェーダが bindless テーブルの配列で引くのに使う。
    std::uint32_t SrvIndex() const;

    // source(同じ大きさ・形式)の中身を自分へそっくりコピーする。自分は ShaderResource 状態になり、
    // source は元の状態へ戻る。大きさか形式が違えば何もせず false。
    bool RecordCopyFrom(const rs::rhi::CommandList& commandList, SceneHdrTarget& source);

    // トーンマップが読むための情報。
    const rs::rhi::SrvHeap& SrvHeapRef() const;
    std::uint64_t SrvGpuHandle() const;

    // 背景を上下のグラデーションにする(見た目の色=sRGB。sRGB空間で補間してリニアのhalf floatで詰める)。
    // Half16 の形式だけ使える。資源がまだ無ければ次の Resize が作り、Resize のたびに作り直す。
    bool SetBackgroundGradient(const float topSrgb[3], const float bottomSrgb[3]);

    // 背景のグラデーションを全面へコピーし、RenderTarget 状態にして返す(BeginScene と違い、RTVのバインドと
    // クリアはしない)。SetBackgroundGradient を呼んでいなければ何もしない。
    void RecordCopyBackground(const rs::rhi::CommandList& commandList);

    // 自分の中身を、同じ大きさ・形式の資源へそっくりコピーし、状態を元に戻す。
    // destinationNative はバックエンドの資源へのポインタ(各バックエンドの資源型)。呼び出し側が
    // COPY_DEST 状態にしてから渡し、コピー後の遷移も呼び出し側が行う。大きさが違えば何もせず false。
    bool RecordCopyTo(const rs::rhi::CommandList& commandList, void* destinationNative);

    // 自分の中身を、読み戻し用バッファ(バックエンドのバッファ資源へのポインタ。COPY_DEST状態)へ行ごとに詰めてコピーし、
    // 状態を元に戻す。rowPitchBytes は 1行の間隔(バックエンドのコピーの行配置の単位の倍数)。
    // 開発用の確認(中身をCPUで読む)と、測光パスの読み戻しが使う。描画は読み戻しの完了を待たない。
    bool RecordCopyToBuffer(const rs::rhi::CommandList& commandList, void* bufferNative,
                            std::uint32_t rowPitchBytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

//
// AmbientOcclusionPass.h - アンビエントオクルージョン(GTAO)のフルスクリーンパス一式
//
// 深度プリパスの出力(PrepassDepthTarget。FrameRenderer が持つ)を読み、そこから GTAO で「見える割合」を求め(AO の生の結果)、
// 深度の差を見る 4x4 のぼかしで均した結果(R8)を作る。結果は、メッシュのシェーダが SRV 番号で引く。
// 品質(AoQuality)ごとの解像度・方向の数・刻みの数は AoPresetSpec の表から決まる。
// 品質が None の間は何も走らず、資源も作らない(一度作った資源は、None に戻しても残す)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "AoQuality.h"
#include "PrepassDepthTarget.h"

namespace rs::render {

// 1フレームぶんの AO の入力(カメラの射影と開発調整値)。
struct AoFrameParams {
    float tanHalfX = 1.0f; // tan(水平視野/2)
    float tanHalfY = 1.0f; // tan(垂直視野/2)
    float nearZ = 0.02f;
    float farZ = 1000.0f;
    float radius = 0.5f;       // 遮蔽を探す半径[m]
    float falloffRange = 0.6f; // 半径の外側の何割で重みを0へ落とすか
    float finalPower = 1.8f;   // 見える割合に掛ける冪
};

class AmbientOcclusionPass {
public:
    // SRV 枠の数(深度・AO の生の結果・ぼかし後の結果)。firstSrvIndex から連続して使う。
    // 先頭(深度)は FrameRenderer が持つ PrepassDepthTarget の枠で、この前提で番号を確保している。このクラスが作るのは後ろの2枠。
    static constexpr std::uint32_t kSrvCount = 3;

    AmbientOcclusionPass();
    ~AmbientOcclusionPass();
    AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass& operator=(const AmbientOcclusionPass&) = delete;

    // PSO・ルート署名と、資源の入れ物を作る。資源そのものは Resize で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 品質と画面の大きさに合わせて資源を(再)生成する。呼ぶ前に GPU が資源を使い終えていること。
    // quality が None なら何もしない(以後 Ready は偽)。同じ品質・大きさなら何もしない。
    bool Resize(std::uint32_t width, std::uint32_t height, AoQuality quality);

    // このフレームに AO を走らせられるか(品質が None でなく、資源が画面の大きさで揃っている)。
    bool Ready(std::uint32_t width, std::uint32_t height) const;

    // 深度プリパスを描き終えたあとに呼ぶ。depth を読む状態にして、GTAO とぼかしを記録する。
    // depth の大きさは Resize に渡した画面の大きさと同じであること。
    // 出力先・ビューポートは書き換えるので、呼び出し側が元へ戻す。
    void Record(const rs::rhi::CommandList& commandList, PrepassDepthTarget& depth, const AoFrameParams& params);

    // 結果(ぼかし後の R8)の SRV 絶対番号。メッシュのシェーダが bindless テーブル で引く。
    std::uint32_t ResultSrvIndex() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

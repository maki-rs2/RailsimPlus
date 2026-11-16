//
// VolumetricFogSkyPass.h - 空の画素へボリュメトリックフォグの格子を当てる全画面のパス
//
// 空パスは影より先に走り、そのときは格子がまだ無いので、不透明の後に当てる。主深度が遠方の画素だけを描き、
// シーンの色を 色 × 透過率 + 散乱光 にする。HDR ターゲットと主深度がバインドされた状態で呼ぶ。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"

namespace rs::render {

class VolumetricFogSkyPass {
public:
    VolumetricFogSkyPass();
    ~VolumetricFogSkyPass();
    VolumetricFogSkyPass(const VolumetricFogSkyPass&) = delete;
    VolumetricFogSkyPass& operator=(const VolumetricFogSkyPass&) = delete;

    bool Init(const rs::rhi::Device& device);
    // integratedSrvIndex は積分の結果の SRV 絶対番号。ルート署名と PSO を差し替えるので、後続は自分の分を積むこと。
    void Record(const rs::rhi::CommandList& commandList, std::uint32_t integratedSrvIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

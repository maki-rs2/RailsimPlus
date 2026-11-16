//
// EnvCubeController.h - 現在のEnvの焼き済みキューブ一式を非同期に読み、GPU資源(EnvCubeSet)へ上げる
//
// ActiveEnv::Generation() が変わるたびに、4つのDDSをワーカースレッド1本で読む(ワーカーはD3D12に触らない)。
// 読み終わった結果はメインスレッドのUpdateで受け取り、GPUの完了を待ってからアップロードする。
// 世代が古い結果は捨てる。複数Envのキャッシュは持たない(常に現在のEnv1つ分)。
// プラグインの空の Sun.x・Moon.x(SkyBodyPass)も同じ流れで読み、同じEnvの世代に合わせてGPUへ上げる(読めなかった天体は描かない。
// キューブの成否には影響しない)。
// プラグインの空(シーンの環境がキューブ付きのとき)は焼き済みキューブをそのまま引く(PluginSkyLighting.h)。
// 標準の空は、鏡面キューブの写真の統計(EnvPhotoStats)を使って明るさを合わせ、鏡面IBLだけに使う。
// 統計はワーカーが読んだ直後に計算する。
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ActiveEnv.h"
#include "../../platform/thread/WorkerThread.h"
#include "../../render/sky/EnvCubeSet.h"
#include "../../render/sky/EnvPhotoStats.h"
#include "../../render/sky/SkyBodyModel.h"
#include "../../render/sky/SkyBodyPass.h"

namespace rs::app {

class EnvCubeController {
public:
    EnvCubeController();

    // 毎フレーム呼ぶ。waitForGpu は、GPUが使っているかもしれないキューブ資源を置き換える・捨てる直前に呼ぶ。
    void Update(const ActiveEnv& activeEnv, rs::render::EnvCubeSet& cubes, rs::render::SkyBodyPass& skyBodies,
                const std::function<void()>& waitForGpu);

    // 現在のEnvのキューブ一式をGPUへ上げ終えていて、使える状態か(プラグインの空を選べるか)。
    bool PluginSkyAvailable() const { return available_; }

    // 現在のEnvの鏡面キューブの統計。キューブを使えない間(切替直後・読込失敗・キューブなし)は ok=false。
    const rs::render::EnvPhotoStats& PhotoStats() const { return photoStats_; }

private:
    struct Request {
        std::uint64_t generation = 0;
        EnvCubePaths paths;
        EnvSkyBodyPaths skyBodyPaths;
    };
    struct Result {
        std::uint64_t generation = 0;
        rs::render::EnvCubeImages images;
        rs::render::SkyBodyImages skyBodies;  // 読めなかった天体は ok=false
        double loadMs = 0.0;
        rs::render::EnvPhotoStats photoStats;  // images.specular が読めたときだけ計算
        double photoStatsMs = 0.0;
        std::string error;  // 空なら読み込みに成功
    };

    static Result Load(const Request& request, std::stop_token stopToken);
    void Apply(Result& result, const ActiveEnv& activeEnv, rs::render::EnvCubeSet& cubes,
               rs::render::SkyBodyPass& skyBodies, const std::function<void()>& waitForGpu);

    std::uint64_t seenGeneration_ = 0;       // 最後に処理したActiveEnvの世代
    bool available_ = false;
    rs::render::EnvPhotoStats photoStats_;
    rs::platform_thread::WorkerThread<Request, Result> worker_;  // 他のメンバが初期化されてから使う。破棄は先に止まる
};

} // namespace rs::app

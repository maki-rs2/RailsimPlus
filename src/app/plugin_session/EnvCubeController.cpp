#include "EnvCubeController.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include "../../render/resource/EnvTextureLoader.h"

namespace rs::app {

namespace {

constexpr std::size_t kDrainBatch = 8;

double ElapsedMs(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

std::string FormatSize(const rs::render::EnvTextureData& data) {
    return std::to_string(data.width) + "x" + std::to_string(data.height);
}

// 3つのうち最初に失敗したものの理由。すべて成功なら空。
std::string FirstLoadError(const rs::render::EnvCubeImages& images) {
    struct Item { const char* label; const rs::render::EnvTextureData* data; };
    const Item items[] = {{"diffuse", &images.diffuse}, {"specular", &images.specular}, {"brdfLut", &images.brdfLut}};
    for (const Item& item : items) {
        if (!item.data->ok) return std::string(item.label) + ": " + item.data->error;
    }
    return {};
}

} // namespace

EnvCubeController::EnvCubeController() : worker_(&EnvCubeController::Load) {}

EnvCubeController::Result EnvCubeController::Load(const Request& request, std::stop_token stopToken) {
    Result result;
    result.generation = request.generation;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    result.images.diffuse = rs::render::LoadEnvTextureDds(request.paths.diffuse);
    if (!stopToken.stop_requested()) result.images.specular = rs::render::LoadEnvTextureDds(request.paths.specular);
    if (!stopToken.stop_requested()) result.images.brdfLut = rs::render::LoadEnvTextureDds(request.paths.brdfLut);
    // Sun.x・Moon.x。指定が無い・読めない天体は ok=false のまま(キューブの成否とは別)。
    if (!stopToken.stop_requested() && !request.skyBodyPaths.sunModel.empty())
        result.skyBodies.sun = rs::render::LoadSkyBodyModel(request.skyBodyPaths.sunModel, request.skyBodyPaths.sunScale);
    if (!stopToken.stop_requested() && !request.skyBodyPaths.moonModel.empty())
        result.skyBodies.moon =
            rs::render::LoadSkyBodyModel(request.skyBodyPaths.moonModel, request.skyBodyPaths.moonScale);
    if (!stopToken.stop_requested() && !request.skyBodyPaths.landscapeModel.empty())
        result.skyBodies.landscape =
            rs::render::LoadSkyBodyModel(request.skyBodyPaths.landscapeModel, request.skyBodyPaths.landscapeScale);
    result.loadMs = ElapsedMs(start);
    result.error = FirstLoadError(result.images);
    if (result.error.empty() && !stopToken.stop_requested()) {
        const std::chrono::steady_clock::time_point statsStart = std::chrono::steady_clock::now();
        result.photoStats = rs::render::ComputeEnvPhotoStats(result.images.specular);
        result.photoStatsMs = ElapsedMs(statsStart);
    }
    if (result.error.empty() && stopToken.stop_requested()) result.error = "cancelled";
    return result;
}

void EnvCubeController::Update(const ActiveEnv& activeEnv, rs::render::EnvCubeSet& cubes,
                               rs::render::SkyBodyPass& skyBodies, const std::function<void()>& waitForGpu) {
    if (activeEnv.Generation() != seenGeneration_) {
        seenGeneration_ = activeEnv.Generation();
        available_ = false;
        photoStats_ = rs::render::EnvPhotoStats{};
        worker_.ClearPending();
        const EnvCubePaths paths = activeEnv.CubePaths();
        if (paths.Empty()) {
            // キューブ指定なし: 前のEnvの資源を捨てて標準の空のままにする。
            if (cubes.Ready() || skyBodies.HasSun() || skyBodies.HasMoon() || skyBodies.HasLandscape()) {
                waitForGpu();
                cubes.Clear();
                skyBodies.Clear();
            }
        } else {
            Request request;
            request.generation = seenGeneration_;
            request.paths = paths;
            request.skyBodyPaths = activeEnv.SkyBodyPaths();
            worker_.Post(std::move(request), 0);
        }
    }

    std::vector<Result> results = worker_.Drain(kDrainBatch);
    for (Result& result : results) {
        if (result.generation != seenGeneration_) continue;  // 古いEnvの結果は捨てる
        Apply(result, activeEnv, cubes, skyBodies, waitForGpu);
    }
}

void EnvCubeController::Apply(Result& result, const ActiveEnv& activeEnv, rs::render::EnvCubeSet& cubes,
                              rs::render::SkyBodyPass& skyBodies, const std::function<void()>& waitForGpu) {
    const std::string& name = activeEnv.Name();
    std::string error = result.error;
    double uploadMs = 0.0;
    if (error.empty()) {
        waitForGpu();
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!cubes.Upload(result.images)) error = cubes.LastError();
        uploadMs = ElapsedMs(start);
    } else {
        waitForGpu();
        cubes.Clear();
    }

    // Sun.x・Moon.x は、キューブが使えなくても上げる(描くかはプラグインの空が有効かで決まる)。GPUの完了は上で待ち済み。
    skyBodies.Upload(result.skyBodies);
    const rs::render::SkyBodyModel& sunBody = result.skyBodies.sun;
    const rs::render::SkyBodyModel& moonBody = result.skyBodies.moon;
    const rs::render::SkyBodyModel& landscapeBody = result.skyBodies.landscape;
    spdlog::info("[env_cube] name={} skyBodies sun={} moon={} landscape={}", name, sunBody.ok ? "ok" : sunBody.error,
                 moonBody.ok ? "ok" : moonBody.error, landscapeBody.ok ? "ok" : landscapeBody.error);

    if (!error.empty()) {
        available_ = false;
        photoStats_ = rs::render::EnvPhotoStats{};
        spdlog::warn("[env_cube] name={} cannot use the baked cubes: {}", name, error);
        return;
    }
    available_ = true;
    photoStats_ = result.photoStats;
    const rs::render::EnvCubeImages& images = result.images;
    spdlog::info("[env_cube] name={} diffuse={} specular={} mips={} brdf={} decode={:.1f}ms upload={:.1f}ms",
                 name, FormatSize(images.diffuse),
                 FormatSize(images.specular), images.specular.mipLevels, FormatSize(images.brdfLut), result.loadMs,
                 uploadMs);
    const rs::render::EnvPhotoStats& stats = result.photoStats;
    spdlog::info("[env_cube] name={} photoStats ok={} sunFound={} skyMean=({:.3f},{:.3f},{:.3f}) "
                 "sunDir=({:.3f},{:.3f},{:.3f}) stats={:.1f}ms",
                 name, stats.ok, stats.sunFound, stats.skyMean[0], stats.skyMean[1], stats.skyMean[2],
                 stats.sunDirection[0], stats.sunDirection[1], stats.sunDirection[2], result.photoStatsMs);
}

} // namespace rs::app

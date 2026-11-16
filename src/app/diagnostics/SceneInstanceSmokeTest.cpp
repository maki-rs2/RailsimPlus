#include "../scene/ParsedPluginCache.h"
#include "SceneInstanceSmokeTest.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../gpu/App.h"
#include "../camera/CameraControl.h"
#include "../plugin_session/PluginSession.h"
#include "../../core/Math.h"

namespace rs::app {

namespace {

bool LoadAndEnsureSceneInstanceBatch(App& app, const std::string& folderUtf8,
                                     const char* logTag) {
    PluginSession smokeSession;
    std::vector<std::string> smokeLog;
    rs::app::GetParsedPluginCache().LoadInto(smokeSession, folderUtf8, smokeLog);
    for (const std::string& line : smokeLog) spdlog::info("[{}] {}", logTag, line);
    if (!smokeSession.plugin.ok || !smokeSession.plugin.hasAssembly) {
        spdlog::error("[{}] load failed or no assembly mesh: {}", logTag, folderUtf8);
        return false;
    }

    bool batchOk = false;
    app.RunUploadCommands([&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
        batchOk = app.InstanceBatches().EnsureBatch(device, upload, folderUtf8,
                                                    smokeSession.plugin.assemblyMesh);
    });
    if (!batchOk) {
        spdlog::error("[{}] EnsureBatch failed for: {}", logTag, folderUtf8);
        return false;
    }
    return true;
}

} // namespace

bool SetupSceneInstanceSmoke(App& app, CameraController& camera,
                             const std::string& folderUtf8, int count) {
    // 背景色をバイト値で判定するので、トーンマップはパススルー(HDR配管の確認を兼ねる)。
    app.SetTonemapPassthrough();
    if (!LoadAndEnsureSceneInstanceBatch(app, folderUtf8, "scene-instance-smoke")) return false;

    CameraView smokeView = camera.BuildView();
    const int clampedCount = std::max(1, count);
    const int gridSide = int(std::ceil(std::sqrt(double(clampedCount))));
    const float spacing = 3.0f;
    int placed = 0;
    for (int gy = 0; gy < gridSide && placed < clampedCount; ++gy) {
        for (int gx = 0; gx < gridSide && placed < clampedCount; ++gx) {
            const rs::core::Mat4 world = rs::core::MakeTranslation(
                smokeView.target[0] + (gx - gridSide * 0.5f) * spacing, smokeView.target[1],
                smokeView.target[2] + (gy - gridSide * 0.5f) * spacing);
            app.InstanceBatches().AddInstance(folderUtf8, world.m);
            ++placed;
        }
    }
    spdlog::info("[scene-instance-smoke] placed {} instances in {} batch(es)",
                app.InstanceBatches().TotalInstanceCount(),
                app.InstanceBatches().BatchCount());
    return true;
}

std::vector<std::pair<std::string, int>> ParseSceneInstanceSmokeBatchSpec(const std::string& spec) {
    std::vector<std::pair<std::string, int>> entries;
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t semi = spec.find(';', pos);
        const std::string token =
            spec.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = (semi == std::string::npos) ? spec.size() + 1 : semi + 1;

        const size_t eq = token.rfind('=');
        if (eq == std::string::npos) continue;
        std::string folder = token.substr(0, eq);
        std::string countStr = token.substr(eq + 1);
        // 前後空白を落とす。
        const auto trim = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
        };
        trim(folder);
        trim(countStr);
        if (folder.empty() || countStr.empty()) continue;
        try {
            entries.emplace_back(folder, std::stoi(countStr));
        } catch (const std::exception&) {
            spdlog::warn("[scene-instance-smoke-batch] invalid count in entry, skipped: {}", token);
        }
    }
    return entries;
}

bool SetupSceneInstanceSmokeBatch(App& app, CameraController& camera,
                                  const std::vector<std::pair<std::string, int>>& entries) {
    app.SetTonemapPassthrough(); // 背景色をバイト値で判定するため
    if (entries.empty()) {
        spdlog::error("[scene-instance-smoke-batch] no entries to place");
        return false;
    }

    CameraView smokeView = camera.BuildView();
    const int outerSide = int(std::ceil(std::sqrt(double(entries.size()))));
    const float outerSpacing = 8.0f;
    const float innerSpacing = 1.0f;

    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& folderUtf8 = entries[i].first;
        const int count = entries[i].second;

        if (!LoadAndEnsureSceneInstanceBatch(app, folderUtf8, "scene-instance-smoke-batch")) continue;

        const int ex = int(i) % outerSide, ey = int(i) / outerSide;
        const float bandX = smokeView.target[0] + (ex - outerSide * 0.5f) * outerSpacing;
        const float bandZ = smokeView.target[2] + (ey - outerSide * 0.5f) * outerSpacing;

        const int clampedCount = std::max(1, count);
        const int innerSide = int(std::ceil(std::sqrt(double(clampedCount))));
        int placed = 0;
        for (int gy = 0; gy < innerSide && placed < clampedCount; ++gy) {
            for (int gx = 0; gx < innerSide && placed < clampedCount; ++gx) {
                const rs::core::Mat4 world = rs::core::MakeTranslation(
                    bandX + (gx - innerSide * 0.5f) * innerSpacing, smokeView.target[1],
                    bandZ + (gy - innerSide * 0.5f) * innerSpacing);
                app.InstanceBatches().AddInstance(folderUtf8, world.m);
                ++placed;
            }
        }
        spdlog::info("[scene-instance-smoke-batch] {}: placed {} instances", folderUtf8, placed);
    }

    spdlog::info("[scene-instance-smoke-batch] total {} instances in {} batch(es) (requested {} "
                "entries, 旧固定上限kMaxInstanceBatches=8を超えるかを確認)",
                app.InstanceBatches().TotalInstanceCount(), app.InstanceBatches().BatchCount(),
                entries.size());
    return app.InstanceBatches().BatchCount() > 0;
}

void EvaluateSceneInstanceSmokeCapture(App& app) {
    std::vector<uint8_t> pixels;
    UINT capW = 0, capH = 0, capRowPitch = 0;
    if (app.TakeCapturedBackBuffer(pixels, capW, capH, capRowPitch)) {
        // クリア色(0.06,0.07,0.10,1.0)のR8G8B8A8_UNORM近似値との距離で判定する。
        const int bgR = 15, bgG = 18, bgB = 26, threshold = 24;
        uint64_t nonBg = 0;
        for (UINT y = 0; y < capH; ++y) {
            const uint8_t* row = pixels.data() + size_t(y) * capRowPitch;
            for (UINT x = 0; x < capW; ++x) {
                const uint8_t* px = row + size_t(x) * 4;
                int dr = int(px[0]) - bgR, dg = int(px[1]) - bgG, db = int(px[2]) - bgB;
                if (dr * dr + dg * dg + db * db > threshold * threshold) ++nonBg;
            }
        }
        double ratio = double(nonBg) / (double(capW) * double(capH));
        spdlog::info("[scene-instance-smoke] non-background pixels: {} / {} ({:.2f}%)",
                    nonBg, uint64_t(capW) * capH, ratio * 100.0);
        spdlog::info("[scene-instance-smoke] RESULT: {}", nonBg > 500 ? "PASS" : "FAIL");
    } else {
        spdlog::error("[scene-instance-smoke] backbuffer capture failed");
        spdlog::info("[scene-instance-smoke] RESULT: FAIL");
    }
    PostQuitMessage(0);
}

} // namespace rs::app

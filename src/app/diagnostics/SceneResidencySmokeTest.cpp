#include "../scene/ParsedPluginCache.h"
#include "SceneResidencySmokeTest.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>

#include "../gpu/App.h"
#include "../camera/CameraControl.h"
#include "../plugin_session/PluginSession.h"
#include "../../core/Math.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/load/runtime/DynamicJointPlacement.h"
#include "../../render/mesh/Mesh.h"

namespace rs::app {

namespace {
// entityWorld(平行移動のみ)+静的パーツ配置からPartXform列を組み立てる。
// SceneRenderer::Tick()と同じ理屈(switchは常に既定値0、moverは進めない)。
std::vector<rs::render::PartXform> BuildStaticPartWorlds(PluginSession& session,
                                                          const rs::core::Mat4& entityWorld) {
    rs::legacy_pi::SwitchResolver switchResolver = [&](const std::string& name) -> int32_t {
        return session.ResolveSwitch(name);
    };
    std::vector<rs::core::Mat4> dynamicWorlds = ResolveDynamicPartWorlds(
        session.plugin.def, session.moverRuntimes, switchResolver, session.SwitchGeneration(),
        0.0f, 0.0f, 0.0);

    std::vector<rs::render::PartXform> xforms;
    xforms.reserve(session.plugin.assemblyPlacements.size());
    for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& pl :
         session.plugin.assemblyPlacements) {
        const int worldPartIndex = pl.WorldPartIndex();
        const rs::core::Mat4 partLocal =
            (worldPartIndex >= 0 && static_cast<size_t>(worldPartIndex) < dynamicWorlds.size())
                ? dynamicWorlds[static_cast<size_t>(worldPartIndex)]
                : rs::core::Mat4::Identity();
        const rs::core::Mat4 world = rs::core::Mat4::Mul(partLocal, entityWorld);
        rs::render::PartXform px;
        std::memcpy(px.world, world.m, sizeof(px.world));
        std::memcpy(px.normalMat, world.m, sizeof(px.normalMat));
        px.normalMat[12] = px.normalMat[13] = px.normalMat[14] = 0.0f;
        xforms.push_back(px);
    }
    return xforms;
}
} // namespace

bool SetupSceneResidencySmoke(App& app, CameraController& camera,
                              const std::string& rootFolderUtf8, int maxSlots) {
    app.SetTonemapPassthrough(); // 背景色をバイト値で判定するため
    std::error_code ec;
    const std::filesystem::path root(rs::platform_text::Utf8ToWide(rootFolderUtf8));
    if (!std::filesystem::is_directory(root, ec)) {
        spdlog::error("[scene-residency-smoke] not a directory: {}", rootFolderUtf8);
        return false;
    }

    std::vector<std::string> candidateFolders;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root, ec)) {
        if (int(candidateFolders.size()) >= std::max(1, maxSlots)) break;
        if (!entry.is_directory()) continue;
        candidateFolders.push_back(rs::platform_text::WideToUtf8(entry.path().wstring()));
    }
    if (candidateFolders.empty()) {
        spdlog::error("[scene-residency-smoke] no subfolders found under: {}", rootFolderUtf8);
        return false;
    }

    CameraView smokeView = camera.BuildView();
    const int gridSide = int(std::ceil(std::sqrt(double(candidateFolders.size()))));
    const float spacing = 6.0f;
    int placed = 0;
    for (size_t i = 0; i < candidateFolders.size(); ++i) {
        const std::string& folderUtf8 = candidateFolders[i];
        std::unique_ptr<PluginSession> session = std::make_unique<PluginSession>();
        std::vector<std::string> log;
        rs::app::GetParsedPluginCache().LoadInto(*session, folderUtf8, log);
        if (!session->plugin.ok || !session->plugin.hasAssembly) {
            spdlog::warn("[scene-residency-smoke] skip (load failed or no assembly): {}",
                        folderUtf8);
            continue;
        }

        const UINT slot = UINT(placed);
        if (!app.UploadSceneMeshToGpu(slot, folderUtf8, session->plugin.assemblyMesh)) {
            spdlog::error("[scene-residency-smoke] UploadSceneMeshToGpu failed at slot {} ({})",
                         slot, folderUtf8);
            break; // SRVプール枯渇。それ以前に確保できたスロット数を結果とする。
        }

        const int gx = int(i) % gridSide, gy = int(i) / gridSide;
        rs::core::Mat4 entityWorld = rs::core::Mat4::Identity();
        entityWorld.m[12] = smokeView.target[0] + (gx - gridSide * 0.5f) * spacing;
        entityWorld.m[13] = smokeView.target[1];
        entityWorld.m[14] = smokeView.target[2] + (gy - gridSide * 0.5f) * spacing;
        app.SetScenePartWorlds(slot, BuildStaticPartWorlds(*session, entityWorld), 0);
        ++placed;
    }

    spdlog::info("[scene-residency-smoke] {} / {} candidate folders placed into distinct "
                "residency slots (旧固定上限8を超えるかを確認)",
                placed, int(candidateFolders.size()));
    if (placed == 0) {
        spdlog::error("[scene-residency-smoke] nothing was placed");
        return false;
    }
    return true;
}

void EvaluateSceneResidencySmokeCapture(App& app) {
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
        spdlog::info("[scene-residency-smoke] non-background pixels: {} / {} ({:.2f}%)",
                    nonBg, uint64_t(capW) * capH, ratio * 100.0);
        spdlog::info("[scene-residency-smoke] RESULT: {}", nonBg > 500 ? "PASS" : "FAIL");
    } else {
        spdlog::error("[scene-residency-smoke] backbuffer capture failed");
        spdlog::info("[scene-residency-smoke] RESULT: FAIL");
    }
    PostQuitMessage(0);
}

} // namespace rs::app

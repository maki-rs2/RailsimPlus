#include "../scene/ParsedPluginCache.h"
#include "TrainInstanceSmokeTest.h"

#include <windows.h>

#include <cstring>
#include <spdlog/spdlog.h>
#include "../gpu/App.h"
#include "../camera/CameraControl.h"
#include "../plugin_session/PluginSession.h"
#include "BackBufferCapturePng.h"
namespace rs::app {
bool SetupTrainInstanceSmoke(App& app, CameraController& camera, const std::string& folder, int count) {
    app.SetTonemapPassthrough(); // 出力をバイト値で数えるため
    PluginSession session; std::vector<std::string> log; rs::app::GetParsedPluginCache().LoadInto(session, folder, log);
    if (!session.plugin.ok || !session.plugin.hasAssembly || session.plugin.assemblyPlacements.empty()) return false;
    bool ok = false; const UINT parts = UINT(session.plugin.assemblyPlacements.size());
    const std::string contentKey = folder + "|" + session.ResolvedGeometrySignature();
    const std::vector<uint32_t>& dynamicMaterialIndices = session.DynamicMaterialIndices();
    app.RunUploadCommands([&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
        ok = app.TrainInstanceBatches().EnsureBatch(
            device, upload, contentKey, session.plugin.assemblyMesh, parts, dynamicMaterialIndices);
    });
    if (!ok) return false;
    const CameraView view = camera.BuildView();
    for (int i = 0; i < (count < 2 ? 2 : count); ++i) {
        std::vector<rs::render::PartXform> worlds(parts, {});
        for (UINT part = 0; part < parts; ++part) {
            worlds[part].world[0] = worlds[part].world[5] = worlds[part].world[10] = worlds[part].world[15] = 1.0f;
            worlds[part].normalMat[0] = worlds[part].normalMat[5] = worlds[part].normalMat[10] = worlds[part].normalMat[15] = 1.0f;
            worlds[part].world[12] = view.target[0] + float(i) * 5.0f;
            worlds[part].world[13] = view.target[1]; worlds[part].world[14] = view.target[2];
            if (part == 0 && (i & 1) != 0) worlds[part].world[13] += 1.0f;
        }
        std::vector<rs::render::InstanceMatOverride> materialOverrides(dynamicMaterialIndices.size());
        for (rs::render::InstanceMatOverride& materialOverride : materialOverrides) {
            materialOverride.diffuse[0] = (i & 1) == 0 ? 1.0f : 0.6f;
            materialOverride.diffuse[1] = (i & 1) == 0 ? 0.6f : 1.0f;
        }
        if (!app.TrainInstanceBatches().AddInstance(contentKey, worlds, materialOverrides, uint64_t(i))) return false;
    }
    spdlog::info("[train-instance-smoke] parts={} instances={} view.target=({:.2f},{:.2f},{:.2f}) "
                 "view.eye=({:.2f},{:.2f},{:.2f})",
                 parts, count < 2 ? 2 : count, view.target[0], view.target[1], view.target[2],
                 view.eye[0], view.eye[1], view.eye[2]);
    return true;
}
void EvaluateTrainInstanceSmokeCapture(App& app, const std::string& capturePngPath,
                                         std::vector<std::string>& log) {
    std::vector<uint8_t> pixels; UINT width=0,height=0,pitch=0;
    if (!app.TakeCapturedBackBuffer(pixels,width,height,pitch)) { spdlog::error("[train-instance-smoke] capture failed"); PostQuitMessage(0); return; }
    // --capture-png併用時: このsmoke自身が消費するのと同じキャプチャ済みバッファを
    // PNGへも保存する(静止画の目視確認用。
    // 通常の--smoke+--capture-pngはフレーム0で即終了するため、この経路とは別扱い)。
    if (!capturePngPath.empty()) SaveBackBufferPng(capturePngPath, pixels, width, height, pitch, log);
    size_t nonBackground=0; for (uint8_t value : pixels) if (value > 8) ++nonBackground; const bool pass=nonBackground>500; spdlog::info("[train-instance-smoke] pixels={} result={}", nonBackground, pass ? "PASS" : "FAIL"); PostQuitMessage(0);
}
}

#include "FrameProfiler.h"

#include "../../platform/EnvironmentVariable.h"
#include <cstdlib>
#include <fstream>
#include <iomanip>

#include <spdlog/spdlog.h>

namespace rs::app {

FrameProfiler::FrameProfiler()
    : enabled_(false), nextSlot_(0), nextFrameIndex_(0), flushed_(false) {
    const char* envPath = rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME");
    if (envPath != nullptr) {
        enabled_ = true;
        outputPath_ = envPath;
        records_.reserve(kMaxRecords);
    }
}

void FrameProfiler::Record(double deltaTimeMs, int gameTicks, double sceneTickMs,
                           double trainTickMs, double sceneryTickMs, double trainSearchMs,
                           double trainKinematicsMs, double carPartWorldMs,
                           double trainRuntimeMs, double trainSubmitMs, double carBatchMs,
                           double carFallbackMs, size_t recomputedSlots,
                           double cpuFrameMs, double presentMs,
                           double frameWaitMs, double gpuFrameMs, double gpuOpaqueMs,
                           double gpuTransMs, double gpuCompositeMs, double gpuImguiMs,
                           double gpuTonemapMs, double gpuAtmosphereMs, double gpuIblMs,
                           double gpuShadowMs, double gpuBloomMs, double gpuFxaaMs, double gpuPrepassMs,
                           double gpuAoMs, double gpuMotionBlurMs, double gpuDepthOfFieldMs, double gpuVolumetricFogMs,
                           const CpuRenderBreakdown& cpuRender) {
    if (!enabled_) return;
    const FrameProfileRecord record{nextFrameIndex_, deltaTimeMs, gameTicks,
                                    sceneTickMs,      trainTickMs, sceneryTickMs,
                                    trainSearchMs,  trainKinematicsMs, carPartWorldMs,
                                    trainRuntimeMs, trainSubmitMs, carBatchMs,
                                    carFallbackMs, recomputedSlots, cpuFrameMs, presentMs,
                                    frameWaitMs,      gpuFrameMs,     gpuOpaqueMs,
                                    gpuTransMs,       gpuCompositeMs, gpuImguiMs,
                                    gpuTonemapMs,     gpuAtmosphereMs, gpuIblMs,
                                    gpuShadowMs,      gpuBloomMs,     gpuFxaaMs,
                                    gpuPrepassMs,     gpuAoMs,        gpuMotionBlurMs, gpuDepthOfFieldMs, gpuVolumetricFogMs, cpuRender};
    ++nextFrameIndex_;
    if (records_.size() < kMaxRecords) {
        records_.push_back(record);
    } else {
        // リング上書き: 上限到達後は最も古いスロットから順に上書きする。
        records_[nextSlot_] = record;
        nextSlot_ = (nextSlot_ + 1) % kMaxRecords;
    }
}

void FrameProfiler::Flush() {
    if (!enabled_ || flushed_) return;
    flushed_ = true;

    std::ofstream out(outputPath_);
    if (!out.is_open()) {
        spdlog::warn("[profile-frame] CSV出力に失敗しました: {}", outputPath_);
        return;
    }
    out << "frameIndex,deltaTimeMs,gameTicks,sceneTickMs,trainTickMs,sceneryTickMs,trainSearchMs,trainKinematicsMs,carPartWorldMs,trainRuntimeMs,trainSubmitMs,carBatchMs,carFallbackMs,recomputedSlots,cpuFrameMs,presentMs,"
           "frameWaitMs,gpuFrameMs,gpuOpaqueMs,gpuTransMs,gpuCompositeMs,gpuImguiMs,gpuTonemapMs,gpuAtmosphereMs,gpuIblMs,gpuShadowMs,gpuBloomMs,gpuFxaaMs,gpuPrepassMs,gpuAoMs,gpuMotionBlurMs,gpuDepthOfFieldMs,gpuVolumetricFogMs,"
           "appRenderMs,beginFrameMs,environmentUpdateMs,rendererMs,cullMs,batchSyncMs,shadowCpuMs,meshRecordMs,overlay2dMs,rmlUiRecordMs,imguiRecordMs,endFrameMs,"
           "overlayPolylines,overlayPolylinePoints,overlayCircles,overlayRects,overlayTexts\n";
    out << std::fixed << std::setprecision(3);
    // records_.size() < kMaxRecords の間はnextSlot_=0のまま単純追記順、
    // 上限到達後はnextSlot_が最古スロットを指すのでそこから1周する。
    const size_t count = records_.size();
    for (size_t i = 0; i < count; ++i) {
        const FrameProfileRecord& record = records_[(nextSlot_ + i) % count];
        out << record.frameIndex << ',' << record.deltaTimeMs << ',' << record.gameTicks << ','
            << record.sceneTickMs << ',' << record.trainTickMs << ',' << record.sceneryTickMs
            << ',' << record.trainSearchMs << ',' << record.trainKinematicsMs << ','
            << record.carPartWorldMs << ',' << record.trainRuntimeMs << ','
            << record.trainSubmitMs << ',' << record.carBatchMs << ','
            << record.carFallbackMs << ','
            << record.recomputedSlots << ',' << record.cpuFrameMs
            << ',' << record.presentMs << ',' << record.frameWaitMs << ',' << record.gpuFrameMs
            << ',' << record.gpuOpaqueMs << ',' << record.gpuTransMs << ','
            << record.gpuCompositeMs << ',' << record.gpuImguiMs << ',' << record.gpuTonemapMs << ','
            << record.gpuAtmosphereMs << ',' << record.gpuIblMs << ',' << record.gpuShadowMs << ','
            << record.gpuBloomMs << ',' << record.gpuFxaaMs << ',' << record.gpuPrepassMs << ',' << record.gpuAoMs << ',' << record.gpuMotionBlurMs << ',' << record.gpuDepthOfFieldMs << ',' << record.gpuVolumetricFogMs << ','
            << record.cpuRender.appRenderMs << ',' << record.cpuRender.beginFrameMs << ','
            << record.cpuRender.environmentUpdateMs << ',' << record.cpuRender.rendererMs << ','
            << record.cpuRender.cullMs << ',' << record.cpuRender.batchSyncMs << ','
            << record.cpuRender.shadowCpuMs << ',' << record.cpuRender.meshRecordMs << ','
            << record.cpuRender.overlay2dMs << ',' << record.cpuRender.rmlUiRecordMs << ',' << record.cpuRender.imguiRecordMs << ',' << record.cpuRender.endFrameMs << ','
            << record.cpuRender.overlayPolylines << ',' << record.cpuRender.overlayPolylinePoints << ','
            << record.cpuRender.overlayCircles << ',' << record.cpuRender.overlayRects << ','
            << record.cpuRender.overlayTexts << '\n';
    }
}

} // namespace rs::app

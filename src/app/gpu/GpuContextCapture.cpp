//
// GpuContextCapture.cpp - GpuContextのバックバッファ読み戻し(コピー記録とMap)
//
// ヘッドレス検証(--capture-png等)とF12スクリーンショットが共用する。
//
#include "GpuContext.h"

#include <spdlog/spdlog.h>

namespace rs::app {

void GpuContext::RecordSceneOnlyCaptureIfRequested(rs::rhi::CommandList commandList) {
    if (!captureRequested_ || captureRecorded_ || captureStage_ != BackBufferCaptureStage::SceneOnly)
        return;
    if (!commandList.Valid()) return;
    RecordCaptureCopy(commandList.Native());
}

void GpuContext::RecordCaptureCopy(ID3D12GraphicsCommandList* cmdList) {
    captureRecorded_ = true; // 失敗しても同じ要求で再試行しない
    captureBuf_.Reset();
    D3D12_RESOURCE_DESC bbDesc = backBuffers_[frameIndex_]->GetDesc();
    UINT64 totalBytes = 0;
    device_->GetCopyableFootprints(&bbDesc, 0, 1, 0, &captureLayout_, nullptr, nullptr,
                                   &totalBytes);
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = totalBytes;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&captureBuf_)))) {
        captureBuf_.Reset();
        return;
    }

    // COPY_SOURCEへ一時遷移してコピーし、元のRENDER_TARGETへ戻す。
    D3D12_RESOURCE_BARRIER toCopySrc = {};
    toCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopySrc.Transition.pResource = backBuffers_[frameIndex_].Get();
    toCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmdList->ResourceBarrier(1, &toCopySrc);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = captureBuf_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = captureLayout_;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = backBuffers_[frameIndex_].Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER toRtAgain = toCopySrc;
    toRtAgain.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toRtAgain.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmdList->ResourceBarrier(1, &toRtAgain);
}

void GpuContext::ReadBackCapture() {
    captureRequested_ = false;
    if (!captureBuf_) return; // 記録されなかった(captured_は無効のまま)
    WaitForGpu(); // コピー完了を待たないとMapした内容が不定になる
    captured_ = CapturedFrame{};
    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, SIZE_T(captureLayout_.Footprint.RowPitch) *
                                    captureLayout_.Footprint.Height};
    if (SUCCEEDED(captureBuf_->Map(0, &readRange, &mapped))) {
        const uint8_t* src = static_cast<const uint8_t*>(mapped);
        captured_.width = captureLayout_.Footprint.Width;
        captured_.height = captureLayout_.Footprint.Height;
        captured_.rowPitch = captureLayout_.Footprint.RowPitch;
        captured_.pixels.assign(src, src + readRange.End);
        captured_.valid = true;
        D3D12_RANGE noWrite = {0, 0};
        captureBuf_->Unmap(0, &noWrite);
    } else {
        spdlog::error("[capture] 読み戻しバッファのMapに失敗しました");
    }
    captureBuf_.Reset();
}

} // namespace rs::app

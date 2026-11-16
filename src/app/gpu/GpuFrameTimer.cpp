#include "GpuFrameTimer.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace rs::app {

namespace {
constexpr size_t kQueriesPerRegion = 2; // 開始/終了
} // namespace

bool GpuFrameTimer::Init(ID3D12Device* device, ID3D12CommandQueue* queue, UINT ringDepth) {
    valid_ = false;
    ringDepth_ = ringDepth;
    recordedByRing_.assign(ringDepth_, std::array<bool, kMaxGpuRegions>{});

    if (device == nullptr || queue == nullptr || ringDepth_ == 0) return false;

    if (FAILED(queue->GetTimestampFrequency(&timestampFrequency_)) ||
        timestampFrequency_ == 0) {
        spdlog::warn("[gpu-timer] GetTimestampFrequency failed, GPUパス別計測を無効化します");
        return false;
    }

    const UINT totalQueries =
        UINT(kMaxGpuRegions * kQueriesPerRegion * ringDepth_);

    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = totalQueries;
    if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_)))) {
        spdlog::warn("[gpu-timer] CreateQueryHeap failed, GPUパス別計測を無効化します");
        return false;
    }

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = UINT64(totalQueries) * sizeof(UINT64);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readbackBuffer_)))) {
        spdlog::warn("[gpu-timer] CreateCommittedResource(readback) failed, "
                     "GPUパス別計測を無効化します");
        return false;
    }

    // persistent map: CPUはこの範囲を読み取り専用で使う(ResolveQueryDataが
    // 書き込む前は不定値だが、recordedByRing_で「実際に記録済みか」を別管理
    // するため未初期化値を誤読することはない)。
    D3D12_RANGE readRange = {0, 0}; // 書き込みは行わないので範囲指定不要
    void* mapped = nullptr;
    if (FAILED(readbackBuffer_->Map(0, &readRange, &mapped)) || mapped == nullptr) {
        spdlog::warn("[gpu-timer] readback buffer Map failed, GPUパス別計測を無効化します");
        readbackBuffer_.Reset();
        return false;
    }
    mappedReadback_ = static_cast<const UINT64*>(mapped);

    lastMs_.fill(0.0);
    valid_ = true;
    return true;
}

void GpuFrameTimer::BeginFrame(UINT ringIndex) {
    if (!valid_ || ringIndex >= ringDepth_) return;
    currentRing_ = ringIndex;

    const size_t base = size_t(ringIndex) * kMaxGpuRegions * kQueriesPerRegion;
    for (size_t region = 0; region < kMaxGpuRegions; ++region) {
        if (!recordedByRing_[ringIndex][region]) {
            lastMs_[region] = 0.0;
            continue;
        }
        const UINT64 beginTicks = mappedReadback_[base + region * kQueriesPerRegion + 0];
        const UINT64 endTicks = mappedReadback_[base + region * kQueriesPerRegion + 1];
        if (endTicks <= beginTicks) {
            lastMs_[region] = 0.0;
        } else {
            const UINT64 deltaTicks = endTicks - beginTicks;
            lastMs_[region] =
                double(deltaTicks) * 1000.0 / double(timestampFrequency_);
        }
    }
    // 新しいフレームの記録に備えてクリアする(このスロットへEndが呼ばれた
    // 区間だけ、次回のBeginFrame()でtrueとして読み出す)。
    recordedByRing_[ringIndex].fill(false);
}

void GpuFrameTimer::Begin(ID3D12GraphicsCommandList* cmdList, size_t regionIndex) {
    if (!valid_ || regionIndex >= kMaxGpuRegions) return;
    const UINT queryIndex =
        UINT(size_t(currentRing_) * kMaxGpuRegions * kQueriesPerRegion +
             regionIndex * kQueriesPerRegion + 0);
    cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}

void GpuFrameTimer::End(ID3D12GraphicsCommandList* cmdList, size_t regionIndex) {
    if (!valid_ || regionIndex >= kMaxGpuRegions) return;
    const UINT queryIndex =
        UINT(size_t(currentRing_) * kMaxGpuRegions * kQueriesPerRegion +
             regionIndex * kQueriesPerRegion + 1);
    cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    recordedByRing_[currentRing_][regionIndex] = true;
}

void GpuFrameTimer::Resolve(ID3D12GraphicsCommandList* cmdList) {
    if (!valid_) return;
    // 区間ごとにEnd()まで記録済みかが異なる(例: メッシュ未ロード時はopaque等の
    // パスを一切記録しない)。未記録のクエリをResolveするとD3D12デバッグレイヤーが
    // 「Cannot Resolve query that has never been performed」を出すため、記録済みの
    // 区間ごとに個別Resolveする(まとめて1回で済ませない)。
    const size_t ringBase = size_t(currentRing_) * kMaxGpuRegions * kQueriesPerRegion;
    for (size_t region = 0; region < kMaxGpuRegions; ++region) {
        if (!recordedByRing_[currentRing_][region]) continue;
        const UINT startQuery = UINT(ringBase + region * kQueriesPerRegion);
        const UINT64 destOffset = UINT64(startQuery) * sizeof(UINT64);
        cmdList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQuery,
                                  UINT(kQueriesPerRegion), readbackBuffer_.Get(), destOffset);
    }
}

double GpuFrameTimer::LastMs(size_t regionIndex) const {
    if (!valid_ || regionIndex >= kMaxGpuRegions) return 0.0;
    return lastMs_[regionIndex];
}

} // namespace rs::app

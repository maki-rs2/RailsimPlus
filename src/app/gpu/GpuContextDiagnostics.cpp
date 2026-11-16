// GpuContextDiagnostics.cpp - GpuContext のデバイス消失(DRED)とD3D12情報キューのログ出力
// GpuContext.cpp から分離。

#include "GpuContext.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

#include "../../platform/text/Encoding.h"

namespace rs::app {

namespace {
// DREDのAutoBreadcrumbOpを人が読める名前へ変換する(D3D12_AUTO_BREADCRUMB_OPの
// 全列挙をカバー。ログにopコードの数値だけ出しても原因追跡に役立たないため)。
const char* BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return "CopyTiles";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "ResolveSubresource";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRenderTargetView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "ClearUnorderedAccessView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDepthStencilView";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "ExecuteBundle";
        case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BeginSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "EndSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME: return "DecodeFrame";
        case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES: return "ProcessFrames";
        case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT: return "AtomicCopyBufferUINT";
        case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64: return "AtomicCopyBufferUINT64";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION: return "ResolveSubresourceRegion";
        case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE: return "WriteBufferImmediate";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS: return "DispatchRays";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND: return "ExecuteMetaCommand";
        case D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION: return "EstimateMotion";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP: return "ResolveMotionVectorHeap";
        case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION: return "SetProtectedResourceSession";
        case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1: return "DecodeFrame1";
        case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1: return "ProcessFrames1";
        default: return "(unknown)";
    }
}
} // namespace


void GpuContext::LogDredOutput() {
    // device_はDEVICE_REMOVED後もインターフェースとしては生存しており、DRED
    // データの取得に使える(実リソースへはアクセスしない)。
    if (!device_) return;
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(device_.As(&dred))) {
        spdlog::warn("[dred] ID3D12DeviceRemovedExtendedData1 not available "
                     "(古いドライバ/OSの可能性)");
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
        // 直近のコマンドリスト(通常は末尾=クラッシュ時実行中だったもの)から
        // 最後に開始/完了したbreadcrumb opを報告する。node一覧は新しいものほど
        // 先頭に来る(DREDの仕様)。
        int nodeIdx = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
             node != nullptr; node = node->pNext, ++nodeIdx) {
            const UINT32 count = node->BreadcrumbCount;
            const UINT32 lastCompleted = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            spdlog::critical("[dred] node[{}] '{}': {} ops recorded, last completed index={}",
                             nodeIdx,
                             node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "?",
                             count, lastCompleted);
            // last completed の直後(=実行中に死んだ可能性が高いop)を中心に前後を出す。
            const UINT32 lo = lastCompleted > 3 ? lastCompleted - 3 : 0;
            const UINT32 hi = std::min<UINT32>(count, lastCompleted + 4);
            for (UINT32 i = lo; i < hi; ++i) {
                std::string opStr = BreadcrumbOpName(node->pCommandHistory[i]);
                if (i == lastCompleted) opStr += " <-- last completed";
                spdlog::critical("[dred]   op[{}] = {}", i, opStr);
            }
            if (nodeIdx > 8) break; // ログ肥大化防止(通常1-2ノードで十分)
        }
        if (nodeIdx == 0) spdlog::warn("[dred] breadcrumb node なし(ロスト時に実行中のコマンドリストが無い)");
    } else {
        spdlog::warn("[dred] GetAutoBreadcrumbsOutput1 failed (breadcrumb data not captured)");
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)) &&
        pageFault.PageFaultVA != 0) {
        spdlog::critical("[dred] page fault VA=0x{:016X}", pageFault.PageFaultVA);
        int i = 0;
        for (const D3D12_DRED_ALLOCATION_NODE1* n = pageFault.pHeadExistingAllocationNode;
             n != nullptr; n = n->pNext, ++i) {
            spdlog::critical("[dred]   existing allocation[{}]: '{}' type={}", i,
                             n->ObjectNameA ? n->ObjectNameA : "?",
                             static_cast<int>(n->AllocationType));
            if (i > 8) break;
        }
        i = 0;
        for (const D3D12_DRED_ALLOCATION_NODE1* n = pageFault.pHeadRecentFreedAllocationNode;
             n != nullptr; n = n->pNext, ++i) {
            spdlog::critical("[dred]   recently freed allocation[{}]: '{}' type={}", i,
                             n->ObjectNameA ? n->ObjectNameA : "?",
                             static_cast<int>(n->AllocationType));
            if (i > 8) break;
        }
    }
}

void GpuContext::LogUploadFailure(const char* what) {
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
    spdlog::critical("[render] ResourceUploadBatch failed: {} (GetDeviceRemovedReason=0x{:08X})",
                     what, static_cast<unsigned long>(reason));
    DrainInfoQueue();
    if (reason != S_OK) MarkDeviceLost("ResourceUploadBatch", reason);
}

void GpuContext::MarkDeviceLost(const char* where, HRESULT hr) {
    // 連打を避けるため、初回だけログへ出す。
    if (deviceLost_) return;
    deviceLost_ = true;
    deviceLostReason_ = device_ ? device_->GetDeviceRemovedReason() : hr;
    spdlog::critical("[render] {}: DXGI device lost (hr=0x{:08X}, GetDeviceRemovedReason=0x{:08X})", where,
                     static_cast<unsigned long>(hr), static_cast<unsigned long>(deviceLostReason_));
    LogDredOutput();
}

void GpuContext::SimulateDeviceLost() {
    ComPtr<ID3D12Device5> device5;
    if (!device_ || FAILED(device_.As(&device5))) {
        spdlog::warn("[render] ID3D12Device5 を取得できないため、デバイスロストを再現できません");
        return;
    }
    spdlog::warn("[render] 開発用: デバイスを取り除きます");
    device5->RemoveDevice();
}

void GpuContext::DrainInfoQueue() {
    if (!infoQueue_) return;
    UINT64 n = infoQueue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &len)) || len == 0) continue;
        std::vector<uint8_t> buf(len);
        D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(infoQueue_->GetMessage(i, msg, &len))) continue;
        std::string text(msg->pDescription, msg->pDescription + msg->DescriptionByteLength);
        if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
            msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
            spdlog::critical("[d3d12] {}", text);
        } else if (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
            spdlog::warn("[d3d12] {}", text);
        }
    }
    infoQueue_->ClearStoredMessages();
}


} // namespace rs::app

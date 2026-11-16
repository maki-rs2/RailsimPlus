//
// HeadlessDevice.cpp - HeadlessDevice.h の実装
//
#include "HeadlessDevice.h"

#include <dxgi1_6.h>

#include <vector>

#include <spdlog/spdlog.h>

namespace rs::app {

Microsoft::WRL::ComPtr<ID3D12Device> CreateHeadlessDevice() {
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))) return nullptr;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
         ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            return device;
    }
    return nullptr;
}

int ReportDebugMessages(ID3D12Device* device, const char* tag) {
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) return 0;
    int count = 0;
    const UINT64 total = infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < total; ++i) {
        SIZE_T length = 0;
        infoQueue->GetMessage(i, nullptr, &length);
        std::vector<char> storage(length);
        D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue->GetMessage(i, message, &length))) continue;
        if (message->Severity > D3D12_MESSAGE_SEVERITY_WARNING) continue; // 情報・メッセージは数えない
        spdlog::warn("[{}] D3D12: {}", tag, message->pDescription);
        ++count;
    }
    return count;
}

} // namespace rs::app

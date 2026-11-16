//
// HeadlessDevice.h - ウィンドウなしのヘッドレス検証(--thumbnail、--tonemap-check)が共用するD3D12デバイスの作成
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace rs::app {

// ウィンドウなしでD3D12デバイスを作る(Debugビルドはデバッグレイヤーを有効にする)。
// ソフトウェアアダプタは使わない。作れなければ nullptr。
Microsoft::WRL::ComPtr<ID3D12Device> CreateHeadlessDevice();

// デバッグレイヤーの警告・エラーを「[tag] D3D12: …」としてログへ出し、その件数を返す(Releaseビルドは常に0)。
int ReportDebugMessages(ID3D12Device* device, const char* tag);

} // namespace rs::app

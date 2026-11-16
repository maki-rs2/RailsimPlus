//
// DeviceLostResult.h - デバイスロストを表す HRESULT の判定(DX12に依存しない純関数)
//
#pragma once

#include <cstdint>

namespace rs::app {

// 描画デバイスの「取り外し・ハング・リセット・ドライバ内部エラー」を表す結果(0x887A0005・0006・0007・0020)。
// Present などが返すと、以降そのデバイスでは何も描けない(作り直しはしない)。
constexpr bool IsDeviceLostResult(std::int32_t hr) {
    const std::uint32_t code = static_cast<std::uint32_t>(hr);
    return code == 0x887A0005u || code == 0x887A0006u || code == 0x887A0007u || code == 0x887A0020u;
}

} // namespace rs::app

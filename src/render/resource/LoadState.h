#pragma once
#include <cstdint>
namespace rs::render {
enum class LoadState : uint8_t {
    kNotLoaded,
    kQueued,
    kLoadingCpu,
    kUploading,
    kReady,
    kFailed,
};
} // namespace rs::render

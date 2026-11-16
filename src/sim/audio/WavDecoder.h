#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rs::sim {

struct DecodedSound {
    std::vector<int16_t> pcm;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
};

class WavDecoder {
public:
    bool Decode(const std::wstring& path, DecodedSound& decoded) const;
};

} // namespace rs::sim

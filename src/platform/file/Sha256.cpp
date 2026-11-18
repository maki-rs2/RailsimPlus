#include "Sha256.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

namespace rs::platform_file {
namespace {

constexpr std::size_t kReadChunkBytes = std::size_t{64} * 1024;
constexpr std::size_t kSha256Bytes = 32;
constexpr char kHexDigits[] = "0123456789abcdef";

// BCryptのハンドルを、戻り値の成否にかかわらず閉じるための管理。
struct AlgorithmCloser {
    void operator()(BCRYPT_ALG_HANDLE handle) const { BCryptCloseAlgorithmProvider(handle, 0); }
};
struct HashCloser {
    void operator()(BCRYPT_HASH_HANDLE handle) const { BCryptDestroyHash(handle); }
};
using AlgorithmHandle = std::unique_ptr<void, AlgorithmCloser>;
using HashHandle = std::unique_ptr<void, HashCloser>;

}  // namespace

std::optional<std::string> Sha256HexOfStream(std::istream& stream) {
    BCRYPT_ALG_HANDLE rawAlgorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return std::nullopt;
    const AlgorithmHandle algorithm(rawAlgorithm);
    BCRYPT_HASH_HANDLE rawHash = nullptr;
    if (BCryptCreateHash(algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0) < 0) return std::nullopt;
    const HashHandle hash(rawHash);

    std::vector<char> buffer(kReadChunkBytes);
    while (stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || stream.gcount() > 0) {
        const ULONG count = static_cast<ULONG>(stream.gcount());
        if (BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(buffer.data()), count, 0) < 0) return std::nullopt;
    }
    // 終端以外の理由で読み込みが止まったときは、途中までのハッシュを返さない。
    if (!stream.eof()) return std::nullopt;

    std::array<std::uint8_t, kSha256Bytes> digest{};
    if (BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return std::nullopt;
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex.push_back(kHexDigits[byte >> 4]);
        hex.push_back(kHexDigits[byte & 0x0F]);
    }
    return hex;
}

std::optional<std::string> Sha256HexOfFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return std::nullopt;
    return Sha256HexOfStream(stream);
}

}  // namespace rs::platform_file

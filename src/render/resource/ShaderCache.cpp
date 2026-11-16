#include "ShaderCache.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>

#include <spdlog/spdlog.h>

#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/file/FileRead.h"
#include "../../platform/file/UserDataDirectory.h"
#include "../../platform/text/Encoding.h"

namespace rs::render {
namespace {

// 64bit FNV-1a の定数(Fowler/Noll/Vo の公開値)。
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
// 2つ目のハッシュ用に、オフセット基底だけを変える(黄金比由来の定数との排他的論理和)。
constexpr std::uint64_t kSecondarySeedMask = 0x9E3779B97F4A7C15ULL;

// ファイルの先頭4バイト。
constexpr char kMagic[4] = {'R', 'S', 'S', 'C'};
// ファイル形式やキーの詰め方を変えたら上げる。キーのハッシュにも混ぜるので、古いファイルは名前からして引かれない。
constexpr std::uint32_t kFormatVersion = 1;
// マジック(4)+形式(4)+primary(8)+secondary(8)+ペイロード長(8)+ペイロードのハッシュ(8)。
constexpr std::size_t kHeaderSize = 40;

constexpr char kCacheDirectoryName[] = "shader_cache";
constexpr char kDisableEnvironmentVariable[] = "RS_SHADER_CACHE";
constexpr char kDisableValue[] = "off";

class Fnv1a {
public:
    explicit Fnv1a(std::uint64_t seed) : state_(seed) {}
    void AddBytes(const void* data, std::size_t size) {
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            state_ ^= bytes[i];
            state_ *= kFnvPrime;
        }
    }
    void AddUint64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            const std::uint8_t byte = static_cast<std::uint8_t>(value >> shift);
            AddBytes(&byte, 1);
        }
    }
    // 長さを前に付けて詰める。
    void AddField(std::string_view field) {
        AddUint64(field.size());
        AddBytes(field.data(), field.size());
    }
    std::uint64_t Value() const { return state_; }

private:
    std::uint64_t state_;
};

std::uint64_t HashKeyWithSeed(const ShaderCacheKey& key, std::uint64_t seed) {
    Fnv1a hash(seed);
    hash.AddUint64(kFormatVersion);
    hash.AddField(key.compiler);
    hash.AddField(key.source);
    hash.AddField(key.entry);
    hash.AddField(key.target);
    hash.AddUint64(key.options.size());
    for (const std::string& option : key.options) hash.AddField(option);
    return hash.Value();
}

std::uint64_t HashPayload(std::span<const std::uint8_t> payload) {
    Fnv1a hash(kFnvOffsetBasis);
    hash.AddBytes(payload.data(), payload.size());
    return hash.Value();
}

void AppendUint(std::vector<std::uint8_t>& out, std::uint64_t value, int byteCount) {
    for (int i = 0; i < byteCount; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint64_t ReadUint(std::span<const std::uint8_t> data, std::size_t offset, int byteCount) {
    std::uint64_t value = 0;
    for (int i = 0; i < byteCount; ++i) value |= static_cast<std::uint64_t>(data[offset + i]) << (8 * i);
    return value;
}

bool CacheDisabledByEnvironment() {
    const char* value = rs::platform::ReadEnvironmentVariable(kDisableEnvironmentVariable);
    return value != nullptr && std::string_view(value) == kDisableValue;
}

// キャッシュのフォルダ。作れなければ空(その場合はキャッシュを使わない)。警告は1回だけ出す。
const std::optional<std::filesystem::path>& CacheDirectory() {
    static const std::optional<std::filesystem::path> kDirectory = []() -> std::optional<std::filesystem::path> {
        const std::filesystem::path path = rs::platform_file::UserDataDirectory() / kCacheDirectoryName;
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error || !std::filesystem::is_directory(path, error)) {
            spdlog::warn("[shader_cache] フォルダを作れないため、キャッシュを使わずにコンパイルします: {}",
                         rs::platform_text::WideToUtf8(path.wstring()));
            return std::nullopt;
        }
        return path;
    }();
    return kDirectory;
}

bool ReadWholeFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out) {
    std::optional<std::vector<std::uint8_t>> bytes = rs::platform_file::ReadFileBytes(path);
    if (!bytes) return false;
    out = std::move(*bytes);
    return true;
}

std::atomic<std::uint32_t> gHitCount{0};
std::atomic<std::uint32_t> gCompileCount{0};

}  // namespace

ShaderCacheHashes HashShaderCacheKey(const ShaderCacheKey& key) {
    ShaderCacheHashes hashes;
    hashes.primary = HashKeyWithSeed(key, kFnvOffsetBasis);
    hashes.secondary = HashKeyWithSeed(key, kFnvOffsetBasis ^ kSecondarySeedMask);
    return hashes;
}

std::string ShaderCacheFileName(std::uint64_t primaryHash) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string name(16, '0');
    for (int i = 0; i < 16; ++i) name[15 - i] = kHexDigits[(primaryHash >> (4 * i)) & 0xF];
    return name + ".bin";
}

std::vector<std::uint8_t> EncodeShaderCacheFile(const ShaderCacheHashes& hashes,
                                                std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> file;
    file.reserve(kHeaderSize + payload.size());
    file.insert(file.end(), std::begin(kMagic), std::end(kMagic));
    AppendUint(file, kFormatVersion, 4);
    AppendUint(file, hashes.primary, 8);
    AppendUint(file, hashes.secondary, 8);
    AppendUint(file, payload.size(), 8);
    AppendUint(file, HashPayload(payload), 8);
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

bool DecodeShaderCacheFile(std::span<const std::uint8_t> file, const ShaderCacheHashes& expected,
                           std::vector<std::uint8_t>& payload) {
    payload.clear();
    if (file.size() < kHeaderSize) return false;
    if (std::memcmp(file.data(), kMagic, sizeof(kMagic)) != 0) return false;
    if (ReadUint(file, 4, 4) != kFormatVersion) return false;
    if (ReadUint(file, 8, 8) != expected.primary) return false;
    if (ReadUint(file, 16, 8) != expected.secondary) return false;
    const std::uint64_t payloadSize = ReadUint(file, 24, 8);
    if (payloadSize != file.size() - kHeaderSize) return false;
    const std::span<const std::uint8_t> body = file.subspan(kHeaderSize);
    if (ReadUint(file, 32, 8) != HashPayload(body)) return false;
    payload.assign(body.begin(), body.end());
    return true;
}

bool LoadOrCompileShader(const ShaderCacheKey& key,
                         const std::function<bool(std::vector<std::uint8_t>&)>& compile,
                         std::vector<std::uint8_t>& bytecode) {
    const bool useCache = !CacheDisabledByEnvironment() && CacheDirectory().has_value();
    std::filesystem::path path;
    ShaderCacheHashes hashes;
    if (useCache) {
        hashes = HashShaderCacheKey(key);
        path = *CacheDirectory() / ShaderCacheFileName(hashes.primary);
        std::vector<std::uint8_t> file;
        if (ReadWholeFile(path, file) && DecodeShaderCacheFile(file, hashes, bytecode)) {
            gHitCount.fetch_add(1);
            return true;
        }
    }

    bytecode.clear();
    gCompileCount.fetch_add(1);
    if (!compile(bytecode)) return false;
    if (!useCache) return true;

    const std::vector<std::uint8_t> file = EncodeShaderCacheFile(hashes, bytecode);
    std::string error;
    if (!rs::platform_file::WriteFileAtomically(
            path, std::string_view(reinterpret_cast<const char*>(file.data()), file.size()), error)) {
        // 書けなくてもコンパイル結果は使える。読み取り専用の環境などで毎回出さないよう、警告は1回だけ。
        static std::once_flag warned;
        std::call_once(warned, [&error]() {
            spdlog::warn("[shader_cache] 保存に失敗しました(以後の失敗は表示しません): {}", error);
        });
    }
    return true;
}

ShaderCacheStats GetShaderCacheStats() {
    return {gHitCount.load(), gCompileCount.load()};
}

}  // namespace rs::render

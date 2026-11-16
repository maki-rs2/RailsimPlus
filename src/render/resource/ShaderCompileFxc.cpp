#include "ShaderCompileFxc.h"

#include <cstring>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <windows.h>

#include "ShaderCache.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {
namespace {

constexpr wchar_t kD3dCompilerDllName[] = L"d3dcompiler_47.dll";

// 読み込まれている d3dcompiler_47.dll のファイルバージョン(取れなければ空)。
std::string D3dCompilerFileVersion() {
    const HMODULE module = ::GetModuleHandleW(kD3dCompilerDllName);
    if (module == nullptr) return {};
    wchar_t modulePath[MAX_PATH] = {};
    if (::GetModuleFileNameW(module, modulePath, MAX_PATH) == 0) return {};
    const DWORD infoSize = ::GetFileVersionInfoSizeW(modulePath, nullptr);
    if (infoSize == 0) return {};
    std::vector<char> info(infoSize);
    if (!::GetFileVersionInfoW(modulePath, 0, infoSize, info.data())) return {};
    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!::VerQueryValueW(info.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoSize) ||
        fixedInfo == nullptr) {
        return {};
    }
    return std::to_string(HIWORD(fixedInfo->dwFileVersionMS)) + "." + std::to_string(LOWORD(fixedInfo->dwFileVersionMS)) +
           "." + std::to_string(HIWORD(fixedInfo->dwFileVersionLS)) + "." + std::to_string(LOWORD(fixedInfo->dwFileVersionLS));
}

// FXCの識別。ヘッダ側のD3D_COMPILER_VERSIONと、実際に読み込まれたDLLのバージョンの両方を入れる。
const std::string& FxcCompilerId() {
    static const std::string kId =
        "fxc " + std::to_string(D3D_COMPILER_VERSION) + " " + D3dCompilerFileVersion();
    return kId;
}

} // namespace

ComPtr<ID3DBlob> CompileFxcCached(const char* source, std::size_t sourceSize, const char* sourceName,
                                  const D3D_SHADER_MACRO* defines, const char* entry, const char* target,
                                  UINT flags, const char* logTag) {
    ShaderCacheKey key;
    key.compiler = FxcCompilerId();
    key.source = std::string_view(source, sourceSize);
    key.entry = entry;
    key.target = target;
    key.options.push_back("flags=" + std::to_string(flags));
    // ソース名はデバッグ情報(D3DCOMPILE_DEBUG)に埋まるので、結果に影響することがある。
    key.options.push_back(std::string("name=") + sourceName);
    for (const D3D_SHADER_MACRO* define = defines; define != nullptr && define->Name != nullptr; ++define) {
        key.options.push_back(std::string("-D") + define->Name + "=" + (define->Definition ? define->Definition : ""));
    }

    std::vector<std::uint8_t> bytecode;
    const bool ok = LoadOrCompileShader(
        key,
        [&](std::vector<std::uint8_t>& out) -> bool {
            ComPtr<ID3DBlob> code, error;
            if (FAILED(D3DCompile(source, sourceSize, sourceName, defines, nullptr, entry, target, flags, 0,
                                  &code, &error))) {
                if (error) {
                    spdlog::error("[{}] {}: {}", logTag, entry, static_cast<const char*>(error->GetBufferPointer()));
                }
                return false;
            }
            const std::uint8_t* data = static_cast<const std::uint8_t*>(code->GetBufferPointer());
            out.assign(data, data + code->GetBufferSize());
            return true;
        },
        bytecode);
    if (!ok) return nullptr;

    ComPtr<ID3DBlob> blob;
    if (FAILED(D3DCreateBlob(bytecode.size(), &blob))) return nullptr;
    std::memcpy(blob->GetBufferPointer(), bytecode.data(), bytecode.size());
    return blob;
}

} // namespace rs::render

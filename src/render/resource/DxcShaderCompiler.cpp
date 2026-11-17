#include "DxcShaderCompiler.h"

#include <objbase.h>

#include <cstdint>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"
#include "ShaderCache.h"
#include "BindlessHlsl.h"

namespace rs::render {

using Microsoft::WRL::ComPtr;

namespace {

// DXCの識別(major.minor と、取れればコミット情報)。キャッシュのキーに入る。
std::string DxcCompilerId(IDxcCompiler3* compiler) {
    std::string id = "dxc";
    ComPtr<IDxcVersionInfo> versionInfo;
    if (FAILED(compiler->QueryInterface(IID_PPV_ARGS(&versionInfo)))) return id;
    UINT32 major = 0;
    UINT32 minor = 0;
    if (SUCCEEDED(versionInfo->GetVersion(&major, &minor))) {
        id += " " + std::to_string(major) + "." + std::to_string(minor);
    }
    ComPtr<IDxcVersionInfo2> versionInfo2;
    if (SUCCEEDED(compiler->QueryInterface(IID_PPV_ARGS(&versionInfo2)))) {
        UINT32 commitCount = 0;
        char* commitHash = nullptr;
        if (SUCCEEDED(versionInfo2->GetCommitInfo(&commitCount, &commitHash)) && commitHash != nullptr) {
            id += " " + std::to_string(commitCount) + " " + commitHash;
            ::CoTaskMemFree(commitHash);  // GetCommitInfoはCoTaskMemAllocで確保した文字列を返す
        }
    }
    return id;
}

} // namespace

bool DxcShaderCompiler::Init() {
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_))) ||
        FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)))) {
        spdlog::error("[dxc] DXC初期化に失敗(dxcompiler.dllが見つかりません)");
        return false;
    }
    compilerId_ = DxcCompilerId(compiler_.Get());
    return true;
}

bool DxcShaderCompiler::Compile(const std::string& source, const wchar_t* entry, const wchar_t* target,
                                const std::vector<std::wstring>& defines, const char* logTag,
                                ComPtr<IDxcBlob>& blob) const {
    if (!utils_ || !compiler_) return false;
    const std::string fullSource = std::string(kBindlessHlsl) + source;
    ComPtr<IDxcBlobEncoding> srcBlob;
    if (FAILED(utils_->CreateBlob(fullSource.c_str(), UINT32(fullSource.size()), DXC_CP_UTF8, &srcBlob))) return false;
    DxcBuffer srcBuffer = {};
    srcBuffer.Ptr = srcBlob->GetBufferPointer();
    srcBuffer.Size = srcBlob->GetBufferSize();
    srcBuffer.Encoding = DXC_CP_UTF8;

    // 全シェーダを HLSL 2021 でそろえる。
    std::vector<LPCWSTR> args = {L"-E", entry, L"-T", target, L"-HV", L"2021"};
    for (const std::wstring& define : defines) {
        args.push_back(L"-D");
        args.push_back(define.c_str());
    }
#if defined(_DEBUG)
    args.push_back(L"-Zi");
    args.push_back(L"-Od");
#else
    args.push_back(L"-O3");
#endif

    // キャッシュのキー。コンパイル結果を変えうる引数(-E/-T/-HV/-D/-Zi/-Od/-O3)を全部、順序どおりに入れる。
    ShaderCacheKey cacheKey;
    cacheKey.compiler = compilerId_;
    cacheKey.source = fullSource;
    cacheKey.entry = rs::platform_text::WideToUtf8(entry);
    cacheKey.target = rs::platform_text::WideToUtf8(target);
    for (LPCWSTR arg : args) cacheKey.options.push_back(rs::platform_text::WideToUtf8(arg));

    std::vector<std::uint8_t> bytecode;
    const bool loaded = LoadOrCompileShader(
        cacheKey,
        [&](std::vector<std::uint8_t>& object) -> bool {
            ComPtr<IDxcResult> result;
            if (FAILED(compiler_->Compile(&srcBuffer, args.data(), UINT32(args.size()), nullptr,
                                          IID_PPV_ARGS(&result))))
                return false;
            HRESULT status = S_OK;
            result->GetStatus(&status);
            if (FAILED(status)) {
                ComPtr<IDxcBlobUtf8> errors;
                result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                if (errors && errors->GetStringLength() > 0)
                    spdlog::error("[{}] dxc: {}", logTag, errors->GetStringPointer());
                return false;
            }
            ComPtr<IDxcBlob> objectBlob;
            if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objectBlob), nullptr))) return false;
            const std::uint8_t* data = static_cast<const std::uint8_t*>(objectBlob->GetBufferPointer());
            object.assign(data, data + objectBlob->GetBufferSize());
            return true;
        },
        bytecode);
    if (!loaded) return false;

    // バイナリなのでコードページは指定なし(DXC_CP_ACP=0)。CreateBlobは中身をコピーする。
    ComPtr<IDxcBlobEncoding> bytecodeBlob;
    if (FAILED(utils_->CreateBlob(bytecode.data(), UINT32(bytecode.size()), DXC_CP_ACP, &bytecodeBlob)))
        return false;
    return SUCCEEDED(bytecodeBlob.As(&blob));
}

} // namespace rs::render

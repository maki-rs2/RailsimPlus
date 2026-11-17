// DxcShaderCompiler.h - DXC(SM6.0)で HLSL をコンパイルする共通部品
//
// メッシュ描画(MeshShaderCompile)とポストプロセスの各パスが使う。結果はディスクのキャッシュ(ShaderCache)を通す。
// 1回の初期化で DXC のインスタンスを作り、複数のエントリポイントをコンパイルする。
#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace rs::render {

class DxcShaderCompiler {
public:
    // DXC を初期化する。dxcompiler.dll が無いなど、使えなければ false。
    bool Init();

    // source の entry を target(例 "ps_6_0")でコンパイルする。defines は "名前=値" の形。
    // 共通の bindless 配列を先頭に連結し、HLSL 2021 で、Debug は最適化なし、Release は -O3。
    // logTag はエラーログの接頭辞。source は呼び出しの間だけ生きていればよい。
    bool Compile(const std::string& source, const wchar_t* entry, const wchar_t* target,
                 const std::vector<std::wstring>& defines, const char* logTag,
                 Microsoft::WRL::ComPtr<IDxcBlob>& blob) const;

private:
    Microsoft::WRL::ComPtr<IDxcUtils> utils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
    std::string compilerId_;
};

} // namespace rs::render

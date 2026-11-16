//
// ShaderCompileFxc.h - FXC(D3DCompile)でのコンパイルをシェーダキャッシュ経由にする共通部分。
//
// DX12型を使うので、render層の.cppからだけ include する内部用ヘッダ(公開ヘッダには出さない)。
//
#pragma once

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstddef>

namespace rs::render {

// D3DCompile と同じ引数で、結果をシェーダキャッシュ(ShaderCache)から引く/保存する。
// definesはD3D_SHADER_MACROの配列(末尾は{nullptr,nullptr})か、無ければnullptr。
// コンパイルに失敗したら、`[logTag] entry: メッセージ` をエラーログへ出してnullptrを返す。
// キーには、コンパイラの識別(FXCとd3dcompiler_47.dllのバージョン)・ソース・エントリ・ターゲット・
// フラグ・ソース名・マクロ定義を入れる。
Microsoft::WRL::ComPtr<ID3DBlob> CompileFxcCached(const char* source, std::size_t sourceSize,
                                                  const char* sourceName,
                                                  const D3D_SHADER_MACRO* defines, const char* entry,
                                                  const char* target, UINT flags, const char* logTag);

} // namespace rs::render

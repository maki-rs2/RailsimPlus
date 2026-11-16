//
// ShaderCache.h - コンパイル済みシェーダ(バイトコード)のディスクキャッシュ。
//
// 起動のたびにシェーダをソースからコンパイルすると約2秒かかるので、結果を
// %LOCALAPPDATA%\RailsimPlus\shader_cache へ保存し、2回目以降は読み込むだけにする。
// DX12型を公開しない(FXC/DXCの呼び出しは呼び出し側が受け持ち、ここはバイト列だけ扱う)。
// 環境変数 RS_SHADER_CACHE=off のときは、読みも書きもせずに毎回コンパイルする(計測用)。
//
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rs::render {

// キャッシュのキー。コンパイル結果を変えることがあるものは、すべてここへ入れる。
// 入れ忘れると、設定を変えても古いバイトコードが使われ続ける。
struct ShaderCacheKey {
    std::string compiler;              // コンパイラの識別(種類+バージョン。呼び出し側が作る)
    std::string_view source;           // 最終的なHLSLソース(includeは使わないので、これだけで内容が決まる)
    std::string entry;
    std::string target;
    std::vector<std::string> options;  // フラグ・マクロ定義・-HV など、結果に影響するものを順序どおりに
};

// キーの2種類のハッシュ。primaryはファイル名に使い、secondaryは読み込み時の衝突検出用。
struct ShaderCacheHashes {
    std::uint64_t primary = 0;
    std::uint64_t secondary = 0;
};

// キーから64bit FNV-1a でハッシュを求める。実行のたびに同じ値になる(std::hashは使わない)。
// 各フィールドは長さを前に付けて詰めるので、境界がずれた別のキーとは衝突しない。
ShaderCacheHashes HashShaderCacheKey(const ShaderCacheKey& key);

// ファイル名(ハッシュの16桁の16進+.bin)。
std::string ShaderCacheFileName(std::uint64_t primaryHash);

// キャッシュファイルの中身(マジック・形式のバージョン・2つのハッシュ・ペイロード長・ペイロードのハッシュ・ペイロード)を作る。
std::vector<std::uint8_t> EncodeShaderCacheFile(const ShaderCacheHashes& hashes,
                                                std::span<const std::uint8_t> payload);

// ファイルの中身を全部確かめて、ペイロードを取り出す。マジック・形式のバージョン・ハッシュ・長さ・
// ペイロードのハッシュのどれか1つでも合わなければ false(payloadは空にする)。
bool DecodeShaderCacheFile(std::span<const std::uint8_t> file, const ShaderCacheHashes& expected,
                           std::vector<std::uint8_t>& payload);

// キャッシュにあればそれをbytecodeへ返し、無ければcompileを呼んで結果を保存する。
// compileはバイトコードをoutへ入れてtrueを返す(失敗時のエラーログは呼び出し側が出す)。
// フォルダが作れない・書けないときは、キャッシュを使わずにコンパイルだけして続ける。
// スレッドから同時に呼んでよい。
bool LoadOrCompileShader(const ShaderCacheKey& key,
                         const std::function<bool(std::vector<std::uint8_t>&)>& compile,
                         std::vector<std::uint8_t>& bytecode);

// キャッシュから読めた回数とコンパイルした回数(起動からの累計)。
struct ShaderCacheStats {
    std::uint32_t hits = 0;
    std::uint32_t compiles = 0;
};
ShaderCacheStats GetShaderCacheStats();

}  // namespace rs::render

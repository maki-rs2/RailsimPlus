//
// texture_loader.h - DirectXTex による画像ロード(PNG/JPG/BMP 等 WIC 形式 + DDS/TGA)
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rs::render {

struct ImageData {
    uint32_t              width = 0;
    uint32_t              height = 0;
    std::vector<uint8_t>  pixels; // R8G8B8A8, width*height*4
    bool                  ok = false;
    // ok=false時のみ有効: ファイル自体が存在しないか(true)、存在はするがデコードに
    // 失敗したか(false、壊れたファイル/未対応形式等)を区別する診断用フラグ。
    bool                  notFound = false;
};

// UTF-8 パスの画像を DirectXTex で読み R8G8B8A8 に展開する。
// 拡張子で WIC(.png/.jpg 等)/DDS/TGA を振り分け、圧縮形式は展開してから正規化する。
// 内部でパス(+更新時刻)キーの LRU キャッシュを持ち、同一画像の再デコードを避ける。
ImageData LoadImageRGBA(const std::string& utf8Path);

// メモリ上の画像(PNG/JPEG等のWIC形式)を R8G8B8A8 に展開する。キャッシュは使わない。
ImageData DecodeImageBytesRGBA(const void* data, size_t size);

// 画像に alpha==0 の texel(抜き穴/透明背景)が1つでもあるかを返す。
// LoadImageRGBA のキャッシュ経由でデコードするため、後段のアップロードで同じ
// 画像を読む場合の追加コストは走査分のみ。判定結果はパス(+更新時刻)ごとに
// 覚えるので、同じ画像の2回目以降は走査もしない。
// パスが空なら false、読み込みに失敗した場合は true を返す(判定不能な画像を
// 「穴なし」と断定して clip を外すより、余分に clip する方が安全なため)。
bool ImageHasZeroAlphaTexel(const std::string& utf8Path);

} // namespace rs::render

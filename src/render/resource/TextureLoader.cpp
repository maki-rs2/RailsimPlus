#include "TextureLoader.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#include <DirectXTex.h>

#include "LruCache.h"
#include "RenderTuning.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"

namespace rs::render {

namespace {
// COM 初期化はプロセスで一度。DirectXTex の WIC 経路は COM を要求する。
struct WicInit {
    bool ok = false;
    WicInit() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
};

// 拡張子(小文字)を返す。'.' は含まない。
std::string LowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    return rs::platform_text::ToAsciiLower(path.substr(dot + 1));
}

// 拡張子に応じて DirectXTex のローダを選択する。DDS/TGA は WIC を経由しない。
HRESULT LoadScratch(const std::wstring& wpath, const std::string& ext,
                    DirectX::ScratchImage& image) {
    if (ext == "dds") {
        return DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE,
                                        nullptr, image);
    }
    if (ext == "tga") {
        return DirectX::LoadFromTGAFile(wpath.c_str(), DirectX::TGA_FLAGS_NONE,
                                        nullptr, image);
    }
    // PNG/JPEG等の色空間メタデータ(sRGB/gAMA/iCCPチャンク)は無視して生テクセルを
    // そのまま読む(WIC_FLAGS_IGNORE_SRGB)。既定(WIC_FLAGS_NONE)だとメタデータ付きの
    // 画像だけ*_UNORM_SRGB形式として読まれ、後段のR8G8B8A8_UNORMへのConvertで
    // sRGB→リニアのガンマ逆変換が掛かって暗くなる。CPUデコード段では生テクセルを
    // 保ち、ガンマの解釈はbase colorの_UNORM_SRGB SRVへ一元化する。これにより
    // 法線マップ・ORMを含め、ファイルのメタデータ有無でテクセル値が変化しない。
    return DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB,
                                    nullptr, image);
}
// 読み込んだ画像を R8G8B8A8 に展開する。
ImageData ToRgbaImage(const DirectX::ScratchImage& loaded);

// 実デコード本体(キャッシュ無し)。DirectXTex で読み R8G8B8A8 に展開する。
ImageData DecodeImageRGBA(const std::string& utf8Path) {
    ImageData out;
    // COMアパートメントはスレッドごとに初期化が必要。ワーカースレッドからも呼ばれるので thread_local にする。
    thread_local WicInit sWic;
    (void)sWic;

    const std::wstring wpath = rs::platform_text::Utf8ToWide(utf8Path);
    const std::string  ext = LowerExtension(utf8Path);

    // LoadFromDDSFile等はファイル未存在も破損ファイルも同じ失敗HRESULTを返すため、
    // 先にファイル存在有無を判定して呼び出し側が原因を区別できるようにする。
    std::error_code ec;
    if (!std::filesystem::exists(wpath, ec)) {
        out.notFound = true;
        return out;
    }

    DirectX::ScratchImage loaded;
    if (FAILED(LoadScratch(wpath, ext, loaded))) return out;
    return ToRgbaImage(loaded);
}

ImageData ToRgbaImage(const DirectX::ScratchImage& loaded) {
    ImageData out;
    // 圧縮(BC等)は展開してから RGBA へ揃える。
    const DirectX::TexMetadata& meta = loaded.GetMetadata();
    DirectX::ScratchImage decompressed;
    const DirectX::ScratchImage* src = &loaded;
    if (DirectX::IsCompressed(meta.format)) {
        if (FAILED(DirectX::Decompress(loaded.GetImages(), loaded.GetImageCount(),
                                       meta, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       decompressed))) {
            return out;
        }
        src = &decompressed;
    }

    // 目的の R8G8B8A8_UNORM へ変換(既に一致していればコピーのみ)。
    DirectX::ScratchImage rgba;
    const DirectX::ScratchImage* result = src;
    if (src->GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(src->GetImages(), src->GetImageCount(),
                                    src->GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM,
                                    DirectX::TEX_FILTER_DEFAULT,
                                    DirectX::TEX_THRESHOLD_DEFAULT, rgba))) {
            return out;
        }
        result = &rgba;
    }

    const DirectX::Image* img = result->GetImage(0, 0, 0);
    if (!img || img->width == 0 || img->height == 0) return out;

    out.width = static_cast<uint32_t>(img->width);
    out.height = static_cast<uint32_t>(img->height);
    out.pixels.resize(static_cast<size_t>(img->width) * img->height * 4);

    // DirectXTex の rowPitch は 4byte 幅と一致しないことがあるため行単位でコピー。
    const size_t dstStride = static_cast<size_t>(img->width) * 4;
    for (size_t y = 0; y < img->height; ++y) {
        std::copy_n(img->pixels + y * img->rowPitch, dstStride,
                    out.pixels.data() + y * dstStride);
    }
    out.ok = true;
    return out;
}

// キャッシュのキーに使う更新時刻。取得に失敗したら既定値(エポック)を返す。
std::filesystem::file_time_type FileMtime(const std::string& utf8Path) {
    std::error_code ec;
    return std::filesystem::last_write_time(
        std::filesystem::path(rs::platform_text::Utf8ToWide(utf8Path)), ec);
}

// パス(+更新時刻)キーのデコード済み画像 LRU キャッシュ。同一テクスチャの
// 再デコード(読込時間の大半を占める無駄)を除く。
// ワーカースレッドからの非同期デコードでも共有できるよう、ミューテックス
// で保護する。設定済みのバイト予算を超えたら LRU で追い出す。
// LRU本体は共通実装 LruCache<Key,Value> に委譲する。
class ImageCache {
public:
    ImageCache()
        : cache_(static_cast<size_t>(RenderTuning::Embedded().textureCacheBudgetBytes),
                 [](const ImageData& img) { return img.pixels.size(); }) {}

    ImageData Get(const std::string& path) {
        const std::filesystem::file_time_type mtime = FileMtime(path);

        ImageData cached;
        if (cache_.TryGet(path, mtime, cached)) {
            return cached; // コピー返し
        }

        // ミス: 重いデコードはロックを保持せずに行う。
        ImageData img = DecodeImageRGBA(path);
        if (!img.ok) return img; // 失敗はキャッシュしない(次回リトライ)
        cache_.Insert(path, mtime, img);
        return img;
    }

private:
    LruCache<std::string, ImageData> cache_;
};

ImageCache& Cache() {
    static ImageCache sCache;
    return sCache;
}

// ImageHasZeroAlphaTexel の判定結果を パス(+更新時刻) で覚える表。
// ChangeModel 候補の多いプラグインでは同じ画像の判定が数千回に及び、そのたびに
// 画素配列のコピーと全画素走査が走って読込時間の大半を占めていた。
// 画素を持たない小さな表なので、ImageCache の LRU とは分ける
// (LRU に入れると判定のために画像を追い出し、再デコードを招く)。
// プラグインのプリフェッチがワーカースレッドから並列に呼ぶため、ミューテックスで保護する。
class ZeroAlphaCache {
public:
    // 覚えていれば true を返して outHasZeroAlpha に結果を入れる。
    bool TryGet(const std::string& path, std::filesystem::file_time_type mtime,
                bool& outHasZeroAlpha) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(path);
        if (it == entries_.end() || it->second.mtime != mtime) return false;
        outHasZeroAlpha = it->second.hasZeroAlpha;
        return true;
    }

    void Insert(const std::string& path, std::filesystem::file_time_type mtime,
                bool hasZeroAlpha) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.insert_or_assign(path, Entry{mtime, hasZeroAlpha});
    }

private:
    struct Entry {
        std::filesystem::file_time_type mtime;
        bool hasZeroAlpha = false;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

ZeroAlphaCache& ZeroAlphaResults() {
    static ZeroAlphaCache sCache;
    return sCache;
}

} // namespace

ImageData DecodeImageBytesRGBA(const void* data, size_t size) {
    thread_local WicInit sWic;
    (void)sWic;
    DirectX::ScratchImage loaded;
    if (FAILED(DirectX::LoadFromWICMemory(static_cast<const uint8_t*>(data), size, DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, loaded))) return {};
    return ToRgbaImage(loaded);
}

ImageData LoadImageRGBA(const std::string& utf8Path) {
    return Cache().Get(utf8Path);
}

bool ImageHasZeroAlphaTexel(const std::string& utf8Path) {
    if (utf8Path.empty()) return false;
    const std::filesystem::file_time_type mtime = FileMtime(utf8Path);
    bool hasZeroAlpha = false;
    if (ZeroAlphaResults().TryGet(utf8Path, mtime, hasZeroAlpha)) return hasZeroAlpha;

    const ImageData img = Cache().Get(utf8Path);
    if (!img.ok) return true; // 失敗は覚えない(ImageCache と同じく次回リトライ)
    for (size_t i = 3; i < img.pixels.size(); i += 4) {
        if (img.pixels[i] == 0) {
            hasZeroAlpha = true;
            break;
        }
    }
    ZeroAlphaResults().Insert(utf8Path, mtime, hasZeroAlpha);
    return hasZeroAlpha;
}

} // namespace rs::render

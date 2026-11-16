//
// FlareTextureSlots.h - レンズフレアの Texture 要素の画像に、LensFlarePass のスロット番号を割り当てる台帳
//
// 太陽(Env)も灯具(プラグイン)も、画像ファイル(絶対パス、UTF-8)をキーに同じ台帳を通す。
// 同じファイルは同じ番号を返す。一度割り当てたスロットは解放しない(Env や Scene が変わっても、
// 新しい画像は新しいスロットへ。使い切ったら以降の画像は割り当てない)。
// GPU・画像の読み込みには触らない。割り当てた画像の読み込みとアップロードは UploadPendingFlareTextures。
//
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../../render/effects/LensFlarePass.h"

namespace rs::app {

class FlareTextureSlots {
public:
    // スロットが足りず割り当てられなかったときの番号(kMaxFlareTextures 以上なので、描画側は描かない)。
    static constexpr std::uint32_t kUnassigned = std::numeric_limits<std::uint32_t>::max();

    // path は空でないこと。割り当て済みなら同じ番号、新規なら次の空きの番号、満杯なら kUnassigned。
    std::uint32_t Assign(const std::string& path);

    struct Pending {
        std::uint32_t slot = 0;
        std::string path;
    };
    // 割り当てたがまだ画像を上げていないスロットを取り出す(取り出したものは、次からは返らない)。
    std::vector<Pending> TakePending();

    // 満杯で割り当てに失敗したことを、初めて問い合わせたときだけ true で返す(警告を1回だけ出すため)。
    bool TakeOverflowNotice();

    std::size_t Count() const { return paths_.size(); }

private:
    std::vector<std::string> paths_;  // 添字がスロット番号
    std::size_t uploadedCount_ = 0;   // paths_ の先頭から、画像を上げ終えた(取り出し済みの)数
    bool overflowed_ = false;
    bool overflowNotified_ = false;
};

}  // namespace rs::app

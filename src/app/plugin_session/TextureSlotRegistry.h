#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace rs::app {

// 絶対パス(UTF-8)をキーにする。割り当てた番号は解放せず、同じ画像には同じ番号を返す。
class TextureSlotRegistry {
public:
    explicit TextureSlotRegistry(std::uint32_t capacity) : capacity_(capacity) {}
    static constexpr std::uint32_t kUnassigned = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t Assign(const std::string& path);
    struct Pending {
        std::uint32_t slot = 0;
        std::string path;
    };
    // 取り出した画像は、読み込みの成否によらず再び返さない。
    std::vector<Pending> TakePending();
    bool TakeOverflowNotice();
    std::size_t Count() const { return paths_.size(); }

private:
    std::uint32_t capacity_;
    std::vector<std::string> paths_;
    std::size_t uploadedCount_ = 0;
    bool overflowed_ = false;
    bool overflowNotified_ = false;
};

} // namespace rs::app

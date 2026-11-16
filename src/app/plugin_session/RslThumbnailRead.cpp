#include "RslThumbnailRead.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace rs::app {
namespace {

// 最上位のinfoの中のthumbnailを探す。falseを返すと解析がそこで止まる。
class ThumbnailSax final : public nlohmann::json_sax<nlohmann::json> {
public:
    std::string thumbnail;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool string(string_t& value) override {
        if (depth_ == 2 && inInfo_ && key_ == "thumbnail") {
            thumbnail = std::move(value);
            return false;
        }
        return true;
    }
    bool start_object(std::size_t) override { return Enter(); }
    bool end_object() override { return Leave(); }
    bool start_array(std::size_t) override { return Enter(); }
    bool end_array() override { return Leave(); }
    bool key(string_t& value) override {
        key_ = value;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) override { return false; }

private:
    bool Enter() {
        ++depth_;
        if (depth_ == 2 && key_ == "info") inInfo_ = true;
        return true;
    }
    bool Leave() {
        // infoが閉じたら、残りは読まない。
        if (depth_ == 2 && inInfo_) return false;
        --depth_;
        return true;
    }

    int depth_ = 0;
    bool inInfo_ = false;
    std::string key_;
};

} // namespace

std::string ReadRslThumbnailBase64(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    ThumbnailSax sax;
    nlohmann::json::sax_parse(stream, &sax);
    return std::move(sax.thumbnail);
}

} // namespace rs::app

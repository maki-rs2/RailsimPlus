#include "Encoding.h"
#include "EncodingBackend.h"

#include <iconv.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>

namespace rs::platform_text {

namespace {

// iconvの変換1回分。不正な入力は1バイト(または1文字)飛ばして replacement を出す。
class Converter {
public:
    Converter(const char* to, const char* from) : handle_(::iconv_open(to, from)) {}
    ~Converter() {
        if (handle_ != kInvalid) ::iconv_close(handle_);
    }
    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;

    // inUnit: 入力の1単位のバイト数(飛ばす幅)。replacement: 不正入力の代わりに出すバイト列。
    std::string Run(const char* in, size_t inBytes, size_t inUnit, const std::string& replacement) {
        std::string out;
        if (handle_ == kInvalid) return out;
        char buffer[4096];
        while (inBytes > 0) {
            char* outPtr = buffer;
            size_t outLeft = sizeof(buffer);
            const size_t result = ::iconv(handle_, const_cast<char**>(&in), &inBytes, &outPtr, &outLeft);
            out.append(buffer, sizeof(buffer) - outLeft);
            if (result != static_cast<size_t>(-1)) break;
            if (errno == E2BIG) continue;
            if (errno != EILSEQ && errno != EINVAL) break;
            // 不正入力。飛ばして置き換え文字を出す。
            const size_t skip = inBytes < inUnit ? inBytes : inUnit;
            in += skip;
            inBytes -= skip;
            out += replacement;
            ::iconv(handle_, nullptr, nullptr, nullptr, nullptr);
        }
        char* outPtr = buffer;
        size_t outLeft = sizeof(buffer);
        ::iconv(handle_, nullptr, nullptr, &outPtr, &outLeft);
        out.append(buffer, sizeof(buffer) - outLeft);
        return out;
    }

private:
    static inline const iconv_t kInvalid = reinterpret_cast<iconv_t>(-1);
    iconv_t handle_;
};

std::wstring BytesToWide(const std::string& bytes, const char* from) {
    if (bytes.empty()) return {};
    Converter converter("WCHAR_T", from);
    const wchar_t kReplacement = 0xFFFD;
    const std::string replacement(reinterpret_cast<const char*>(&kReplacement), sizeof(wchar_t));
    const std::string raw = converter.Run(bytes.data(), bytes.size(), 1, replacement);
    std::wstring out(raw.size() / sizeof(wchar_t), L'\0');
    std::copy(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(out.size() * sizeof(wchar_t)),
              reinterpret_cast<char*>(out.data()));
    return out;
}

std::string WideToBytes(const std::wstring& wide, const char* to) {
    if (wide.empty()) return {};
    Converter converter(to, "WCHAR_T");
    return converter.Run(reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t),
                         sizeof(wchar_t), "?");
}

} // namespace

std::wstring Cp932ToWide(const std::string& sjis) {
    return BytesToWide(sjis, "CP932");
}

std::string WideToCp932(const std::wstring& wide) {
    return WideToBytes(wide, "CP932");
}

std::wstring Utf8ToWide(const std::string& utf8) {
    return BytesToWide(utf8, "UTF-8");
}

std::string WideToUtf8(const std::wstring& wide) {
    return WideToBytes(wide, "UTF-8");
}

} // namespace rs::platform_text

#include "Encoding.h"
#include "EncodingBackend.h"

#include <windows.h>

namespace rs::platform_text {

namespace {
constexpr UINT kCp932 = 932;

std::string WideToCodePage(const std::wstring& wide, UINT cp) {
    if (wide.empty()) return {};
    int n = ::WideCharToMultiByte(cp, 0, wide.data(), int(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(size_t(n), '\0');
    ::WideCharToMultiByte(cp, 0, wide.data(), int(wide.size()), out.data(), n, nullptr,
                          nullptr);
    return out;
}

std::wstring CodePageToWide(const std::string& bytes, UINT cp) {
    if (bytes.empty()) return {};
    int n = ::MultiByteToWideChar(cp, 0, bytes.data(), int(bytes.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(size_t(n), L'\0');
    ::MultiByteToWideChar(cp, 0, bytes.data(), int(bytes.size()), out.data(), n);
    return out;
}
} // namespace

std::wstring Cp932ToWide(const std::string& sjis) {
    return CodePageToWide(sjis, kCp932);
}

std::string WideToCp932(const std::wstring& wide) {
    return WideToCodePage(wide, kCp932);
}

std::wstring Utf8ToWide(const std::string& utf8) {
    return CodePageToWide(utf8, CP_UTF8);
}

std::string WideToUtf8(const std::wstring& wide) {
    return WideToCodePage(wide, CP_UTF8);
}

} // namespace rs::platform_text

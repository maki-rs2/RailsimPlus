#include "EnvironmentVariable.h"

#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>

#include "text/Encoding.h"
#else
#include <cstdlib>
#endif

namespace rs::platform {

const char* ReadEnvironmentVariable(const char* name, bool developmentOnly) {
#if !RAILSIM_DEV_ENV
    if (developmentOnly) {
        (void)name;
        return nullptr;
    }
#else
    (void)developmentOnly;
#endif
    if (name == nullptr) return nullptr;
    thread_local std::unordered_map<std::string, std::string> values;
    std::string& value = values[name];
#ifdef _WIN32
    const std::wstring wideName = rs::platform_text::Utf8ToWide(name);
    const DWORD requiredSize = ::GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (requiredSize == 0) return nullptr;
    std::wstring wideValue(requiredSize, L'\0');
    const DWORD copiedSize = ::GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), requiredSize);
    if (copiedSize == 0 || copiedSize >= requiredSize) return nullptr;
    wideValue.resize(copiedSize);
    value = rs::platform_text::WideToUtf8(wideValue);
#else
    const char* rawValue = ::getenv(name);
    if (rawValue == nullptr) return nullptr;
    value = rawValue;
#endif
    return value.c_str();
}

} // namespace rs::platform

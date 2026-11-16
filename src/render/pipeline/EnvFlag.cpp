#include "EnvFlag.h"

#include "../../platform/EnvironmentVariable.h"
#include <cstdlib>

namespace rs::render {

bool ParseEnvFlag(const char* envValue, bool defaultValue) {
    if (envValue == nullptr || envValue[0] == '\0') return defaultValue;
    return !(envValue[0] == '0' && envValue[1] == '\0');
}

bool ReadEnvFlag(const char* name, bool defaultValue) {
    return ParseEnvFlag(rs::platform::ReadEnvironmentVariable(name), defaultValue);
}

std::optional<float> ReadEnvFloat(const char* name) {
    const char* text = rs::platform::ReadEnvironmentVariable(name);
    if (text == nullptr) return std::nullopt;
    return static_cast<float>(std::atof(text));
}

bool PbrUnifiedEnabled() {
    static const bool kEnabled = ReadEnvFlag("RS_PBR_UNIFIED", true);
    return kEnabled;
}

} // namespace rs::render

#include "ScreenshotFileName.h"

#include <cstdio>

namespace rs::app {

std::string FormatScreenshotFileName(const std::tm& localTime, int suffix) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "railsimplus_%04d%02d%02d_%02d%02d%02d",
                  localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
                  localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    std::string name = buffer;
    if (suffix >= 2) name += "_" + std::to_string(suffix);
    return name + ".png";
}

std::string ChooseScreenshotFileName(const std::tm& localTime,
                                     const std::function<bool(const std::string&)>& exists) {
    int suffix = 1;
    std::string name = FormatScreenshotFileName(localTime, suffix);
    while (exists(name)) name = FormatScreenshotFileName(localTime, ++suffix);
    return name;
}

} // namespace rs::app

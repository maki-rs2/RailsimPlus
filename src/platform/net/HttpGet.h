#pragma once

#include <optional>
#include <string>

namespace rs::platform_net {

std::optional<std::string> HttpGet(const std::string& url, const std::string& userAgent);
bool IsHttpsUrl(const std::string& url);
bool OpenHttpsUrl(const std::string& url);

} // namespace rs::platform_net

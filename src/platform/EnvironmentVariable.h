#pragma once

namespace rs::platform {

// Returns a pointer to a thread-local UTF-8 copy, or nullptr when unset/disabled.
// The returned pointer remains valid until the next call on the same thread.
const char* ReadEnvironmentVariable(const char* name, bool developmentOnly = true);

} // namespace rs::platform

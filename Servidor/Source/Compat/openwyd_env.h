#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>

inline const char* openwyd_env_string(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

inline int openwyd_env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value)
        return fallback;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
        return fallback;
    return static_cast<int>(parsed);
}

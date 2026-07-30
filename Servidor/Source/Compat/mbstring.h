#pragma once

#include <cstring>

inline unsigned char* _mbsupr(unsigned char* text) {
    if (!text)
        return text;
    for (unsigned char* cursor = text; *cursor; ++cursor) {
        if (*cursor >= 'a' && *cursor <= 'z')
            *cursor = static_cast<unsigned char>(*cursor - ('a' - 'A'));
    }
    return text;
}

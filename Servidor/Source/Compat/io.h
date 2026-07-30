#pragma once

#include <cctype>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef _O_BINARY
#define _O_BINARY 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef _S_IREAD
#define _S_IREAD S_IRUSR
#endif
#ifndef _S_IWRITE
#define _S_IWRITE S_IWUSR
#endif
#ifndef _O_RDONLY
#define _O_RDONLY O_RDONLY
#endif
#ifndef _O_WRONLY
#define _O_WRONLY O_WRONLY
#endif
#ifndef _O_RDWR
#define _O_RDWR O_RDWR
#endif
#ifndef _O_CREAT
#define _O_CREAT O_CREAT
#endif
#ifndef _O_TRUNC
#define _O_TRUNC O_TRUNC
#endif

inline int _open(const char* path, int flags, int mode = 0666) { return ::open(path, flags, mode); }
inline int _read(int fd, void* buffer, unsigned count) { return static_cast<int>(::read(fd, buffer, count)); }
inline int _write(int fd, const void* buffer, unsigned count) { return static_cast<int>(::write(fd, buffer, count)); }
inline int _close(int fd) { return ::close(fd); }
inline long _lseek(int fd, long offset, int origin) { return static_cast<long>(::lseek(fd, offset, origin)); }
inline long _filelength(int fd) {
    struct stat status {};
    return ::fstat(fd, &status) == 0 ? static_cast<long>(status.st_size) : -1L;
}
inline int _mkdir(const char* path) { return ::mkdir(path, 0775); }
inline char* _strupr(char* text) {
    for (char* cursor = text; cursor && *cursor; ++cursor)
        *cursor = static_cast<char>(std::toupper(static_cast<unsigned char>(*cursor)));
    return text;
}

struct _finddata_t {
    unsigned attrib;
    std::time_t time_create;
    std::time_t time_access;
    std::time_t time_write;
    std::int64_t size;
    char name[260];
};

constexpr unsigned _A_SUBDIR = 0x10;

std::intptr_t _findfirst(const char* pattern, _finddata_t* data);
int _findnext(std::intptr_t search, _finddata_t* data);
int _findclose(std::intptr_t search);

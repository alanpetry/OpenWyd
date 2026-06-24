#pragma once

#include <sys/stat.h>

#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#ifndef _S_IWRITE
#define _S_IWRITE S_IWUSR
#endif

struct _stat64i32 {
  long long size;
  long mode;
  long mtime_value;
};

extern "C" int _open(const char*, int, ...);
extern "C" int _close(int);
extern "C" int _read(int, void*, unsigned int);
extern "C" int _write(int, const void*, unsigned int);
extern "C" long _lseek(int, long, int);
extern "C" int _filelength(int);
extern "C" int _stat64i32(const char*, struct _stat64i32*);
extern "C" int _commit(int);

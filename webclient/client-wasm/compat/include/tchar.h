#pragma once

#include <cstring>
#include <cstdio>
#include <strings.h>

using TCHAR = char;

#define _T(x) x
#define TEXT(x) x

#define _tcscpy std::strcpy
#define _tcscat std::strcat
#define _tcsncat std::strncat
#define _tcslen std::strlen
#define _tcsicmp strcasecmp
#define _tcscmp std::strcmp
#define _tcsncpy std::strncpy
#define _tcsrchr std::strrchr
#define _tcschr std::strchr

#define _sntprintf std::snprintf
#define _stscanf std::sscanf

#define _stricmp strcasecmp

inline char* strupr(char* s) {
  if (!s) return s;
  for (char* p = s; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - ('a' - 'A'));
  }
  return s;
}

#ifndef _strupr
#define _strupr strupr
#endif

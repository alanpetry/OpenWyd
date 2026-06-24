#pragma once
#include "windows.h"

using u_short = unsigned short;
using u_int = unsigned int;
using u_long = unsigned long;
using SOCKET = std::uintptr_t;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET ((SOCKET)(~0))
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif

struct in_addr {
  union {
    struct {
      BYTE s_b1;
      BYTE s_b2;
      BYTE s_b3;
      BYTE s_b4;
    } S_un_b;
    struct {
      WORD s_w1;
      WORD s_w2;
    } S_un_w;
    u_long S_addr;
  } S_un;
};

struct sockaddr {
  unsigned short sa_family;
  char sa_data[14];
};

struct sockaddr_in {
  short sin_family;
  unsigned short sin_port;
  in_addr sin_addr;
  char sin_zero[8];
};

struct hostent {
  char* h_name;
  char** h_aliases;
  short h_addrtype;
  short h_length;
  char** h_addr_list;
};


#ifdef __cplusplus
extern "C" {
#endif

unsigned short htons(unsigned short hostshort);
unsigned short ntohs(unsigned short netshort);
unsigned long htonl(unsigned long hostlong);
unsigned long ntohl(unsigned long netlong);

unsigned long inet_addr(const char* cp);
int gethostname(char* name, size_t namelen);

SOCKET socket(int af, int type, int protocol);
int bind(SOCKET s, const sockaddr* name, int namelen);
int connect(SOCKET s, const sockaddr* name, int namelen);
int listen(SOCKET s, int backlog);
int closesocket(SOCKET s);
int send(SOCKET s, const char* buf, int len, int flags);
int recv(SOCKET s, char* buf, int len, int flags);

#ifdef __cplusplus
}
#endif

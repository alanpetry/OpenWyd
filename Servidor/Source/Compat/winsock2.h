#pragma once

#include "Windows.h"

#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SOCKET = int;
using SOCKADDR = struct sockaddr;
using PSOCKADDR = struct sockaddr*;
using SOCKADDR_IN = struct sockaddr_in;
using IN_ADDR = struct in_addr;
using HOSTENT = struct hostent;

struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
};

constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

constexpr long FD_READ = 0x01;
constexpr long FD_WRITE = 0x02;
constexpr long FD_OOB = 0x04;
constexpr long FD_ACCEPT = 0x08;
constexpr long FD_CONNECT = 0x10;
constexpr long FD_CLOSE = 0x20;

#define MAKEWORD(low, high) static_cast<WORD>((static_cast<WORD>(low) & 0xffu) | ((static_cast<WORD>(high) & 0xffu) << 8u))
#define WSAGETSELECTEVENT(value) LOWORD(value)
#define WSAGETSELECTERROR(value) HIWORD(value)

int WSAStartup(WORD requested_version, WSADATA* data);
int WSACleanup();
int WSAGetLastError();
int WSAAsyncSelect(SOCKET socket, HWND window, UINT message, long events);
int closesocket(SOCKET socket);

// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: Winsock names on top of BSD sockets. SOCKET is an fd; errors come from errno through WSAGetLastError.
#pragma once
#ifndef _WIN32
#include "windows.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define WSAEINTR EINTR
#define WSAEACCES EACCES
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAEALREADY EALREADY
#define WSAEMSGSIZE EMSGSIZE
#define WSAEADDRINUSE EADDRINUSE
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAENETDOWN ENETDOWN
#define WSAENETUNREACH ENETUNREACH
#define WSAECONNABORTED ECONNABORTED
#define WSAECONNRESET ECONNRESET
#define WSAENOTCONN ENOTCONN
#define WSAETIMEDOUT ETIMEDOUT
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEHOSTUNREACH EHOSTUNREACH
#define WSAENOTSOCK ENOTSOCK
#define WSAEISCONN EISCONN
#define WSAEINVAL EINVAL
typedef struct WSAData { WORD wVersion; WORD wHighVersion; char szDescription[257]; char szSystemStatus[129]; } WSADATA, *LPWSADATA;
inline int WSAStartup(WORD, LPWSADATA d) { if (d) std::memset(d, 0, sizeof(*d)); return 0; }
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int e) { errno = e; }
inline int closesocket(SOCKET s) { return ::close(s); }
inline int ioctlsocket(SOCKET s, long cmd, u_long* arg) {
    if (cmd == static_cast<long>(FIONBIO)) {
        int fl = fcntl(s, F_GETFL, 0);
        if (fl < 0) return -1;
        return fcntl(s, F_SETFL, *arg ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK)) < 0 ? -1 : 0;
    }
    int v = 0; int r = ::ioctl(s, static_cast<unsigned long>(cmd), &v); *arg = static_cast<u_long>(v); return r;
}
typedef struct pollfd WSAPOLLFD;
inline int WSAPoll(WSAPOLLFD* fds, ULONG n, int ms) { return ::poll(fds, n, ms); }
#define IOC_VENDOR 0x18000000u
#define _WSAIOW(x, y) (0x80000000u | (x) | (y))
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
// WSAIoctl is used only for SIO_UDP_CONNRESET, a Windows-only quirk; Linux UDP sockets don't need it.
inline int WSAIoctl(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD ret, void*, void*) { if (ret) *ret = 0; return 0; }
#endif

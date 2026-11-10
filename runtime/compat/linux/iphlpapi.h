// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: GetAdaptersAddresses over getifaddrs, with the fields the runtime reads (status, type, name, IPv4 addresses
// and prefix length). Everything lives inside the caller's buffer, as on Windows.
#pragma once
#ifndef _WIN32
#include "winsock2.h"
#define GAA_FLAG_SKIP_UNICAST 0x0001u
#define GAA_FLAG_SKIP_ANYCAST 0x0002u
#define GAA_FLAG_SKIP_MULTICAST 0x0004u
#define GAA_FLAG_SKIP_DNS_SERVER 0x0008u
#define GAA_FLAG_INCLUDE_PREFIX 0x0010u
#define IF_TYPE_OTHER 1
#define IF_TYPE_ETHERNET_CSMACD 6
#define IF_TYPE_SOFTWARE_LOOPBACK 24
#define IF_TYPE_IEEE80211 71
typedef enum { IfOperStatusUp = 1, IfOperStatusDown = 2 } IF_OPER_STATUS;
typedef struct _SOCKET_ADDRESS { struct sockaddr* lpSockaddr; INT iSockaddrLength; } SOCKET_ADDRESS;
typedef struct _IP_ADAPTER_UNICAST_ADDRESS {
    ULONG Length; DWORD Flags; struct _IP_ADAPTER_UNICAST_ADDRESS* Next; SOCKET_ADDRESS Address; UCHAR OnLinkPrefixLength;
} IP_ADAPTER_UNICAST_ADDRESS, *PIP_ADAPTER_UNICAST_ADDRESS;
typedef struct _IP_ADAPTER_ADDRESSES {
    ULONG Length; DWORD IfIndex; struct _IP_ADAPTER_ADDRESSES* Next; char* AdapterName; PIP_ADAPTER_UNICAST_ADDRESS FirstUnicastAddress;
    PWSTR FriendlyName; PWSTR Description; DWORD IfType; IF_OPER_STATUS OperStatus; ULONG Mtu;
} IP_ADAPTER_ADDRESSES, *PIP_ADAPTER_ADDRESSES;
ULONG GetAdaptersAddresses(ULONG family, ULONG flags, PVOID, PIP_ADAPTER_ADDRESSES out, PULONG len);
#endif

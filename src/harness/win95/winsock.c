#include "harness/winsock.h"

#ifndef _WIN32

int WSAStartup(int version, WSADATA* data) {
    // very minimal, we don't care about the arguments
    return 0;
}

int WSAGetLastError(void) {
    return errno;
}

int WSACleanup(void) {
    return 0;
}

// Only implement non-blocking call for now
int ioctlsocket(int handle, long cmd, unsigned long* argp) {
    assert(cmd == FIONBIO);
#ifdef __vita__
    uint32_t _true = 1;
    setsockopt(handle, SOL_SOCKET, SCE_NET_SO_NBIO, (char *)&_true, sizeof(uint32_t));
    return 0;
#else
    int flags = fcntl(handle, F_GETFL);
    flags |= O_NONBLOCK;
    return fcntl(handle, F_SETFL, flags);
#endif
}

int closesocket(int handle) {
    return close(handle);
}

#endif

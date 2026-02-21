#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <vitasdk.h>

static SceUID directory_iterator = 0;

void resolve_full_path(char* path, const char* argv0) {
    return;
}

FILE* OS_fopen(const char* pathname, const char* mode) {
    FILE* f = fopen(pathname, mode);
    if (f != NULL) {
        return f;
    }
    return NULL;
}

char* OS_GetNextFileInDirectory(void) {
    SceIoDirent entry;

    if (directory_iterator == NULL) {
        return NULL;
    }
    while (sceIoDread(directory_iterator, &entry) > 0) {
        if (!SCE_S_ISDIR(entry.d_stat.st_mode)) {
            return entry.d_name;
        }
    }
    sceIoDclose(directory_iterator);
    directory_iterator = NULL;
    return NULL;
}

char* OS_GetFirstFileInDirectory(char* path) {
    directory_iterator = sceIoDopen(path);
    if (directory_iterator == NULL) {
        return NULL;
    }
    return OS_GetNextFileInDirectory();
}

size_t OS_ConsoleReadPassword(char* pBuffer, size_t pBufferLen) {
    return 0;
}

char* OS_Basename(const char* path) {
	return "ux0:data/dethrace";
}

char* OS_GetWorkingDirectory(char* argv0) {
	return "ux0:data/dethrace";
}

int OS_GetAdapterAddress(char* name, void* pSockaddr_in) {
    return 1;
}


int OS_InitSockets(void) {
    return 0;
}

int OS_GetLastSocketError(void) {
    return errno;
}

void OS_CleanupSockets(void) {
    // no-op
}

int OS_SetSocketNonBlocking(int socket) {
	char _true = 1;
	return setsockopt(socket, SOL_SOCKET, SO_NONBLOCK, (char *)&_true, sizeof(_true));
}

int OS_CloseSocket(int socket) {
    return close(socket);
}
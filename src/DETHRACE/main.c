
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#include <windows.h>
#endif

#include "brender.h"

extern void Harness_Init(int* argc, char* argv[]);
extern int original_main(int pArgc, char* pArgv[]);

void BR_CALLBACK _BrBeginHook(void) {
    struct br_device* BR_EXPORT BrDrv1SoftPrimBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1SoftRendBegin(char* arguments);

    BrDevAddStatic(NULL, BrDrv1SoftPrimBegin, NULL);
    BrDevAddStatic(NULL, BrDrv1SoftRendBegin, NULL);
    // BrDevAddStatic(NULL, BrDrv1SDL2Begin, NULL);
}

void BR_CALLBACK _BrEndHook(void) {
}

#ifdef __vita__
#include <vitasdk.h>
int _newlib_heap_size_user = 192 * 1024 * 1024;
int dethrace_main(unsigned int argc, void* argv);
int main(int argc, char* argv[]) {
	// We need a bigger stack to run dethrace, so we create a new thread with a proper stack size
	SceUID main_thread = sceKernelCreateThread("dethrace", dethrace_main, 0x40, 0x800000, 0, 0, NULL);
	if (main_thread >= 0){
		sceKernelStartThread(main_thread, 0, NULL);
	}
	return sceKernelExitDeleteThread(0);
}
int dethrace_main(unsigned int vita_argc, void *vita_argv) {
	int argc = 0;
	char *argv[1];
	argv[0] = "";
#else
int main(int argc, char* argv[]) {
#endif
#ifdef _WIN32
    /* Attach to the console that started us if any */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        /* We attached successfully, lets redirect IO to the consoles handles if not already redirected */
        if (_fileno(stdout) == -2 || _get_osfhandle(_fileno(stdout)) == -2) {
            freopen("CONOUT$", "w", stdout);
        }

        if (_fileno(stderr) == -2 || _get_osfhandle(_fileno(stderr)) == -2) {
            freopen("CONOUT$", "w", stderr);
        }

        if (_fileno(stdin) == -2 || _get_osfhandle(_fileno(stdin)) == -2) {
            freopen("CONIN$", "r", stdin);
        }
    }
#endif

    Harness_Init(&argc, argv);

    return original_main(argc, argv);
}

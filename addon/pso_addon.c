/*
 * PSO UE4 client add-on (xinput1_3.dll)
 *
 * Loaded by Pro Soccer Online as xinput1_3.dll from the game's Binaries\Win64 folder.
 * Every XInput export is forwarded to pso_core.dll (the private-server client DLL, not part of this
 * repository), so controllers and the server connection keep working; on top of that this adds:
 *   - a dark menu theme ("Moon") with an in-game Sun / Moon switch          (theme.h)
 *   - coloured name tags                                                    (tags2.h)
 *   - keeping the PSO launcher up to date                                   (launcher_upd.h)
 *
 * Everything runs inside the game process and only changes how the menus look.
 * Build: see README.md.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <winhttp.h>

static char g_dir[MAX_PATH];                          /* the game's Binaries\Win64 folder (with trailing \) */
static HANDLE g_proc;

static char g_logpath[MAX_PATH + 64];                 /* %LOCALAPPDATA%\UE4PSO\pso-addon.log */
static void logf_(const char *fmt, ...) {
    if (!g_logpath[0]) return;
    FILE *f = fopen(g_logpath, "a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

/* ------------------------------------------------------------ safe memory access */
static int rd(uintptr_t a, void *buf, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)a, buf, n, &got) && got == n;
}
typedef struct { uintptr_t ptr; int32_t num, max; } TArr;   /* TArray / FString header */

#include "refl.h"
#include "tags2.h"
#include "theme.h"
#include "launcher_upd.h"

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameA(h, g_dir, MAX_PATH);
    char *s = strrchr(g_dir, '\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    const wchar_t *cmd = GetCommandLineW();
    if (wcsstr(cmd, L"-nullrhi") || wcsstr(cmd, L"-psohost=")) return TRUE;   /* servers: nothing to draw */
    g_proc = GetCurrentProcess();
    char la[MAX_PATH]; DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", la, sizeof la);
    if (n && n < sizeof la - 40) {
        char d[MAX_PATH + 16]; snprintf(d, sizeof d, "%s\\UE4PSO", la); CreateDirectoryA(d, NULL);
        snprintf(g_logpath, sizeof g_logpath, "%s\\UE4PSO\\pso-addon.log", la);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(g_logpath, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 512 * 1024)) DeleteFileA(g_logpath);
    }
    tags_start(); theme_start(); launcher_update_start();
    return TRUE;
}

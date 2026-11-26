/*
 * PSO UE4 client add-on (xinput1_3.dll)
 *
 * Loaded by Pro Soccer Online as xinput1_3.dll from the game's Binaries\Win64 folder.
 * Every XInput export is forwarded to pso_core.dll (the private-server client DLL, not part of this
 * repository), so controllers and the server connection keep working; on top of that this adds:
 *   - a dark menu theme ("Moon") with an in-game Sun / Moon switch          (theme.h)
 *   - coloured name tags and chat colours                                   (tags2.h)
 *   - goal effects: visuals, sounds, the clown on the goalkeeper            (goalfx.h, d3dfx.h, clown3d.h)
 *   - the goal effect picker and the Goal Effects setting in Settings       (goaltab.h, goalvid.h, goalsync.h)
 *   - the region label "NA (Virginia)" in Custom Lobbies                    (region.h)
 *   - stamina that refills 5% faster (the player's own character)           (stamina.h)
 *   - a longer pause between the engine's memory clean-ups (fewer stutters)
 *   - keeping the PSO launcher up to date                                   (launcher_upd.h)
 *
 * Everything runs inside the game process. Build: see README.md.
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

static int is_steamid(const wchar_t *w) {
    if (wcsncmp(w, L"7656119", 7) != 0) return 0;
    for (int i = 0; i < 17; i++) if (w[i] < L'0' || w[i] > L'9') return 0;
    return 1;
}

/* copies "-name=<17 digit id>" from the command line */

/* ------------------------------------------------------------ safe memory access */
/* Reading the game's memory. The add-on looks at a great many game objects, and each look used to be a call into
 * Windows (ReadProcessMemory): safe, but slow enough that the constant looking cost real processor time during
 * matches. Now the bytes are copied directly by a tiny routine, and a handler registered with Windows catches the
 * rare case of a bad address inside that routine only: the copy then simply reports "couldn't read", exactly as
 * before. If the handler can't be set up (or pso-slowread.txt exists in %LOCALAPPDATA%\UE4PSO) the old way is used. */
int pso_rd_copy(void *dst, const void *src, size_t n);
extern char pso_rd_copy_begin[], pso_rd_copy_end[], pso_rd_copy_fail[];
__asm__(".text\n"
        ".globl pso_rd_copy\n"
        "pso_rd_copy:\n"
        "  push %rsi\n"
        "  push %rdi\n"
        "  mov %rcx, %rdi\n"
        "  mov %rdx, %rsi\n"
        "  mov %r8, %rcx\n"
        ".globl pso_rd_copy_begin\n"
        "pso_rd_copy_begin:\n"
        "  rep movsb\n"
        ".globl pso_rd_copy_end\n"
        "pso_rd_copy_end:\n"
        "  mov $1, %eax\n"
        "  pop %rdi\n"
        "  pop %rsi\n"
        "  ret\n"
        ".globl pso_rd_copy_fail\n"
        "pso_rd_copy_fail:\n"
        "  xor %eax, %eax\n"
        "  pop %rdi\n"
        "  pop %rsi\n"
        "  ret\n");
static volatile LONG g_rd_fast;                              /* 0 not set up yet, 2 being set up, 1 on, -1 off */
static LONG CALLBACK rd_veh(EXCEPTION_POINTERS *e) {
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != 0xC0000005u /* bad address */ && c != 0xC0000006u /* page not available */ && c != 0x80000001u /* guard page */) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t ip = (uintptr_t)e->ContextRecord->Rip;
    if (ip < (uintptr_t)pso_rd_copy_begin || ip >= (uintptr_t)pso_rd_copy_end) return EXCEPTION_CONTINUE_SEARCH;      /* not ours: the game deals with it as always */
    e->ContextRecord->Rip = (DWORD64)(uintptr_t)pso_rd_copy_fail;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void rd_fast_init(void) {                             /* from a worker thread, never while the DLL is being loaded */
    if (InterlockedCompareExchange(&g_rd_fast, 2, 0) != 0) return;
    char la[MAX_PATH], f[MAX_PATH + 40]; DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", la, sizeof la);
    if (n && n < sizeof la) { snprintf(f, sizeof f, "%s\\UE4PSO\\pso-slowread.txt", la); if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) { g_rd_fast = -1; return; } }
    if (!AddVectoredExceptionHandler(1, rd_veh)) { g_rd_fast = -1; return; }
    volatile uint64_t probe = 0x1122334455667788ull; uint64_t got = 0, junk = 0;
    int ok = pso_rd_copy(&got, (const void *)&probe, 8) == 1 && got == 0x1122334455667788ull && pso_rd_copy(&junk, (const void *)(uintptr_t)0x10, 8) == 0;      /* a good read works, a bad one is caught */
    g_rd_fast = ok ? 1 : -1;
}
static int rd(uintptr_t a, void *buf, SIZE_T n) {
    if (a < 0x10000 || a > 0x00007FFFFFFEFFFFull || n > 0x4000000) return 0;
    if (g_rd_fast == 1) return pso_rd_copy(buf, (const void *)a, n);
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)a, buf, n, &got) && got == n;
}

typedef struct { uintptr_t ptr; int32_t num, max; } TArr;   /* TArray / FString header */

#include "refl.h"
#include "d3dfx.h"
#include "goalfx.h"
#include "tags2.h"
#include "theme.h"
#include "uidump.h"
#include "goaltab.h"
#include "goalvid.h"
#include "region.h"
#include "goalsync.h"
#include "clown3d.h"
#include "stamina.h"
#include "watchdog.h"
#include "launcher_upd.h"


/* The game's engine tidies its memory ("garbage collection") every 61 seconds, and each time the whole game stops
 * for one long frame (measured: 166 ms at 240 fps, with the add-on idle). That is the regular hitch players feel, on
 * their own PC and, when a match server does it, for everyone in that match at once. The engine reads how often to do
 * this from the game's own settings file when it starts, so the interval is written there before the engine reads it:
 * every 15 minutes instead of every minute. Joining or leaving a match still tidies up, as always.
 * Only that one line is touched; a pso-keep-gc.txt file in %LOCALAPPDATA%\UE4PSO leaves the game's file alone. */
static void gc_interval_setup(void) {
    wchar_t la[MAX_PATH], p[MAX_PATH + 96], cur[32] = L""; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n >= MAX_PATH - 8) return;
    swprintf(p, MAX_PATH + 96, L"%ls\\UE4PSO\\pso-keep-gc.txt", la); if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return;
    static const wchar_t *dirs[] = { L"ProSoccerOnline", L"ProSoccerOnline\\Saved", L"ProSoccerOnline\\Saved\\Config", L"ProSoccerOnline\\Saved\\Config\\WindowsNoEditor" };
    for (int i = 0; i < 4; i++) { swprintf(p, MAX_PATH + 96, L"%ls\\%ls", la, dirs[i]); CreateDirectoryW(p, NULL); }
    swprintf(p, MAX_PATH + 96, L"%ls\\ProSoccerOnline\\Saved\\Config\\WindowsNoEditor\\Engine.ini", la);
    GetPrivateProfileStringW(L"SystemSettings", L"gc.TimeBetweenPurgingPendingKillObjects", L"", cur, 32, p);
    if (wcscmp(cur, L"900")) WritePrivateProfileStringW(L"SystemSettings", L"gc.TimeBetweenPurgingPendingKillObjects", L"900", p);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameA(h, g_dir, MAX_PATH);
    char *s = strrchr(g_dir, '\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    const wchar_t *cmd = GetCommandLineW();
    if (wcsstr(cmd, L"-nullrhi") || wcsstr(cmd, L"-psohost=")) return TRUE;   /* match servers: not part of this repository */
    g_proc = GetCurrentProcess();
    gc_interval_setup();
    char la[MAX_PATH]; DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", la, sizeof la);
    if (n && n < sizeof la - 40) {
        char d[MAX_PATH + 16]; snprintf(d, sizeof d, "%s\\UE4PSO", la); CreateDirectoryA(d, NULL);
        snprintf(g_logpath, sizeof g_logpath, "%s\\UE4PSO\\pso-addon.log", la);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(g_logpath, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 512 * 1024)) DeleteFileA(g_logpath);
    }
    tags_start(); theme_start(); launcher_update_start(); goaltab_start(); goalvid_start(); region_start(); goalfx_start(); goalsync_start(); stamina_start(0); watchdog_start();
    return TRUE;
}

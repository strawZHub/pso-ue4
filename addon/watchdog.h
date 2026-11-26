/* watchdog.h - if the game stops responding, write down where it is stuck (test builds).
 * The game's main thread answers window messages all the time; when it hasn't for 6 seconds, its
 * current position (and what the add-on was doing on it, if anything) goes into the log. Read-only. */
/* (declared earlier: g_hb_tick, g_bc, g_bc_rt_)   heartbeat; what the add-on is doing on the game thread / the draw thread */
static void wd_where(uintptr_t a, char *out, int cap) {
    HMODULE m = NULL; wchar_t p[MAX_PATH]; out[0] = 0;
    if (GetModuleHandleExW(4 /*FROM_ADDRESS*/ | 2 /*UNCHANGED_REFCOUNT*/, (LPCWSTR)a, &m) && m && GetModuleFileNameW(m, p, MAX_PATH)) {
        wchar_t *s = wcsrchr(p, L'\\'); s = s ? s + 1 : p; char n[40]; int z = 0; for (; s[z] && z < 39; z++) n[z] = (char)s[z]; n[z] = 0;
        snprintf(out, cap, "%s+0x%llx", n, (unsigned long long)(a - (uintptr_t)m)); }
}
static DWORD WINAPI wd_thread(LPVOID u) {
    (void)u; int reported = 0;
    for (;;) {
        Sleep(2000);
        DWORD hb = g_hb_tick, now = GetTickCount();
        if (!hb || !g_gt_id) continue;
        if (now - hb < 6000) { if (reported) { logf_("watchdog: the game is responding again"); reported = 0; } continue; }
        if (reported >= 2) continue;
        reported++;
        static uint64_t stk[1024]; CONTEXT cx; memset(&cx, 0, sizeof cx); cx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER; int got = 0, n = 0;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_gt_id);
        if (th) { if (SuspendThread(th) != (DWORD)-1) { if (GetThreadContext(th, &cx)) { got = 1; SIZE_T r = 0; if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cx.Rsp, stk, sizeof stk, &r)) n = (int)(r / 8); } ResumeThread(th); } CloseHandle(th); }
        logf_("watchdog: the game has not responded for %lu s. Add-on step on the game thread: %ld, on the draw thread: %ld", (now - hb) / 1000, g_bc, g_bc_rt_);
        if (got) { char w[80]; wd_where((uintptr_t)cx.Rip, w, sizeof w); logf_("watchdog: stuck at %s", w[0] ? w : "?");
            char line[900] = ""; int shown = 0;
            for (int i = 0; i < n && shown < 40; i++) { wd_where((uintptr_t)stk[i], w, sizeof w); if (!w[0]) continue; size_t L = strlen(line); snprintf(line + L, sizeof line - L, "%s | ", w); shown++; if (shown % 8 == 0) { logf_("watchdog: stack: %s", line); line[0] = 0; } }
            if (line[0]) logf_("watchdog: stack: %s", line); }
    }
}
static void watchdog_start(void) { HANDLE t = CreateThread(NULL, 0, wd_thread, NULL, 0, NULL); if (t) CloseHandle(t); }

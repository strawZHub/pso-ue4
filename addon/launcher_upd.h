/* launcher_upd.h - keeps every player's PSO Launcher up to date.
 * The launcher can't replace itself, but this add-on is updated with the game files, so a while after the
 * game starts it downloads the launcher from the PSO server (http://<server>/download) and, when that one has a
 * higher build number (PSOLAUNCHER_BUILD=n), puts it in place of the player's launcher. A running .exe can be
 * renamed on Windows, so the current one becomes "<name>.old" (removed by the new launcher) and the next start
 * opens the new launcher. */
#include <tlhelp32.h>

static int lu_build(const unsigned char *b, size_t n) {              /* PSOLAUNCHER_BUILD=n; or 0 (old launchers) */
    static const char k[] = "PSOLAUNCHER_BUILD=";
    size_t kl = sizeof k - 1;
    for (size_t i = 0; i + kl + 1 < n; i++)
        if (b[i] == 'P' && !memcmp(b + i, k, kl)) { int v = 0; for (size_t j = i + kl; j < n && b[j] >= '0' && b[j] <= '9'; j++) v = v * 10 + (b[j] - '0'); return v; }
    return 0;
}
static int lu_is_launcher(const unsigned char *b, size_t n) {       /* our launcher: a Windows program with its window class inside */
    static const wchar_t cls[] = L"UE4PSOLauncher";
    if (n < 300000 || n > 20000000 || b[0] != 'M' || b[1] != 'Z') return 0;
    size_t kl = sizeof cls - sizeof(wchar_t);
    for (size_t i = 0; i + kl < n; i++) if (b[i] == 'U' && !memcmp(b + i, cls, kl)) return 1;
    return 0;
}
static unsigned char *lu_read(const wchar_t *p, size_t *n) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0; unsigned char *b = NULL;
    if (sz != INVALID_FILE_SIZE && sz < 20000000 && (b = malloc(sz + 1)) && ReadFile(f, b, sz, &got, NULL) && got == sz) *n = sz;
    else { free(b); b = NULL; }
    CloseHandle(f); return b;
}
static unsigned char *lu_download(const wchar_t *host, size_t *n) {  /* http://host/download */
    unsigned char *buf = NULL; size_t len = 0, cap = 0;
    HINTERNET s = WinHttpOpen(L"pso", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0), c = NULL, r = NULL;
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    DWORD code = 0, cl = sizeof code;
    if ((c = WinHttpConnect(s, host, 80, 0)) && (r = WinHttpOpenRequest(c, L"GET", L"/download", NULL, NULL, NULL, 0))
        && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)
        && WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &cl, NULL) && code == 200) {
        for (;;) {
            if (cap - len < 65536) { size_t nc = cap ? cap * 2 : 2 << 20; if (nc > 24000000) break; unsigned char *nb = realloc(buf, nc); if (!nb) break; buf = nb; cap = nc; }
            DWORD got = 0; if (!WinHttpReadData(r, buf + len, (DWORD)(cap - len), &got) || !got) break;
            len += got;
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    if (!buf || !len) { free(buf); return NULL; }
    *n = len; return buf;
}
static int lu_launcher_path(wchar_t *out, DWORD cap) {              /* the running launcher's .exe */
    HWND w = FindWindowW(L"UE4PSOLauncher", NULL);
    DWORD pid = 0; if (w) GetWindowThreadProcessId(w, &pid);
    if (!pid) return 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    DWORD n = cap; int ok = QueryFullProcessImageNameW(p, 0, out, &n) && n > 4;
    CloseHandle(p); return ok;
}
static int lu_server(wchar_t *host, int cap) {                      /* server= from pso-server.txt next to the game */
    char p[MAX_PATH + 32], line[256]; snprintf(p, sizeof p, "%spso-server.txt", g_dir);
    FILE *f = fopen(p, "r"); int ok = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "server=", 7)) {
        line[strcspn(line, "\r\n \t")] = 0;
        if (line[7]) { MultiByteToWideChar(CP_UTF8, 0, line + 7, -1, host, cap); ok = 1; }
        break;
    }
    fclose(f); return ok;
}
static DWORD WINAPI lu_thread(void *a) {
    (void)a;
    Sleep(30000);                                                    /* let the game start first */
    wchar_t host[128], exe[MAX_PATH * 2], old[MAX_PATH * 2 + 8], tmp[MAX_PATH * 2 + 8];
    if (!lu_server(host, 128)) return 0;
    for (int tries = 0; !lu_launcher_path(exe, MAX_PATH * 2); tries++) { if (tries >= 10) return 0; Sleep(30000); }
    size_t ln = 0, nn = 0; unsigned char *cur = lu_read(exe, &ln);
    if (!cur) return 0;
    int have = lu_is_launcher(cur, ln) ? lu_build(cur, ln) : -1; free(cur);
    if (have < 0) return 0;                                          /* not our launcher: never touched */
    unsigned char *nw = lu_download(host, &nn);
    if (!nw) return 0;
    int got = lu_is_launcher(nw, nn) ? lu_build(nw, nn) : 0;
    if (got > have) {
        swprintf(tmp, MAX_PATH * 2 + 8, L"%ls.new", exe); swprintf(old, MAX_PATH * 2 + 8, L"%ls.old", exe);
        HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD wr = 0; int ok = 0;
        if (f != INVALID_HANDLE_VALUE) { ok = WriteFile(f, nw, (DWORD)nn, &wr, NULL) && wr == nn; CloseHandle(f); }
        if (ok) {
            DeleteFileW(old);
            if (MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING)) {           /* the running launcher steps aside */
                if (MoveFileExW(tmp, exe, MOVEFILE_REPLACE_EXISTING)) logf_("launcher updated: build %d -> %d", have, got);
                else MoveFileExW(old, exe, MOVEFILE_REPLACE_EXISTING);        /* put it back */
            }
        }
        DeleteFileW(tmp);
    }
    free(nw); return 0;
}
static void launcher_update_start(void) { HANDLE t = CreateThread(NULL, 0, lu_thread, NULL, 0, NULL); if (t) { SetThreadPriority(t, THREAD_PRIORITY_LOWEST); CloseHandle(t); } }

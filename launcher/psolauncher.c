/* PSOLauncher.exe - "PSO UE4 Launcher"
 * Installs / updates the private-server version of Pro Soccer Online from the PSO server and starts it.
 * Game goes to %LOCALAPPDATA%\UE4PSO\Game (no admin rights needed, Steam's copy is untouched).
 * Server address: DEFAULT_SERVER below, or "server=..." in PSOLauncher.ini next to the exe. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <objidl.h>

#define DEFAULT_SERVER L"your.server.address"   /* or server=... in PSOLauncher.ini next to the exe */
#define WM_REFRESH (WM_APP + 1)

/* ------------------------------------------------------------ state shared with the UI */
enum { ST_CONNECTING, ST_OFFLINE, ST_CHECKING, ST_NEED, ST_DOWNLOADING, ST_READY, ST_ERROR, ST_WAITING, ST_RUNNING };
static volatile int g_state = ST_CONNECTING;
static CRITICAL_SECTION g_cs;
static wchar_t g_status[256] = L"Connecting to the server...";
static int g_lobbies = -1, g_players = -1, g_online;
static wchar_t *g_notes;
static volatile LONGLONG g_done, g_total;
static volatile int g_installed_once;
static HWND g_wnd;
static wchar_t g_server[128] = DEFAULT_SERVER, g_base[MAX_PATH], g_game[MAX_PATH];

static void set_status(int st, const wchar_t *fmt, ...) {
    EnterCriticalSection(&g_cs);
    if (st >= 0) g_state = st;
    if (fmt) { va_list ap; va_start(ap, fmt); _vsnwprintf(g_status, 255, fmt, ap); va_end(ap); g_status[255] = 0; }
    LeaveCriticalSection(&g_cs);
    PostMessage(g_wnd, WM_REFRESH, 0, 0);
}

/* ------------------------------------------------------------ HTTP (self-signed cert accepted) */
static HINTERNET g_ses;
static HINTERNET open_req(const wchar_t *path, LONGLONG range_from) {
    HINTERNET c = WinHttpConnect(g_ses, g_server, 443, 0);
    if (!c) return NULL;
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); return NULL; }
    DWORD fl = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
             | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &fl, sizeof fl);
    wchar_t hdr[64] = L"";
    if (range_from > 0) swprintf(hdr, 64, L"Range: bytes=%lld-", range_from);
    if (!WinHttpSendRequest(r, hdr[0] ? hdr : WINHTTP_NO_ADDITIONAL_HEADERS, hdr[0] ? (DWORD)-1L : 0, NULL, 0, 0, 0)
        || !WinHttpReceiveResponse(r, NULL)) { WinHttpCloseHandle(r); WinHttpCloseHandle(c); return NULL; }
    return r;   /* connection handle is closed together with the session; fine for this program */
}
static DWORD status_code(HINTERNET r) {
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
    return code;
}
/* whole response into a malloc'd, NUL-terminated buffer */
static char *http_get(const wchar_t *path, DWORD *code) {
    HINTERNET r = open_req(path, 0);
    if (!r) return NULL;
    *code = status_code(r);
    size_t cap = 1 << 16, n = 0; char *buf = malloc(cap);
    for (;;) {
        DWORD got = 0;
        if (n + 65536 + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
        if (!WinHttpReadData(r, buf + n, 65536, &got) || !got) break;
        n += got;
    }
    buf[n] = 0;
    WinHttpCloseHandle(r);
    return buf;
}

/* ------------------------------------------------------------ tiny JSON helpers (server output is simple) */
static const char *json_num(const char *s, const char *key, double *out) {
    char k[64]; snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return NULL;
    p += strlen(k); while (*p == ' ') p++;
    *out = atof(p);
    return p;
}
/* JSON string value -> wide string (handles \n \" \\ \uXXXX); returns malloc'd */
static wchar_t *json_wstr(const char *s, const char *key) {
    char k[64]; snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return NULL;
    p += strlen(k); while (*p == ' ') p++;
    if (*p != '"') return NULL;
    p++;
    wchar_t *out = malloc((strlen(p) * 2 + 2) * sizeof(wchar_t)); int n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[n++] = L'\r'; out[n++] = L'\n'; break;
            case 't': out[n++] = L'\t'; break;
            case 'r': break;
            case 'u': { char hx[5] = {0}; memcpy(hx, p + 1, 4); out[n++] = (wchar_t)strtol(hx, NULL, 16); p += 4; break; }
            default: out[n++] = (wchar_t)(unsigned char)*p;
            }
            p++;
        } else {
            /* UTF-8 pass-through for safety (server sends ASCII + \u escapes) */
            out[n++] = (wchar_t)(unsigned char)*p++;
        }
    }
    out[n] = 0;
    return out;
}

/* ------------------------------------------------------------ files */
typedef struct { char p[400]; LONGLONG s; char h[41]; int need; } FileEnt;
static FileEnt *g_files; static int g_nfiles, g_version;

typedef struct { char p[400]; LONGLONG s; LONGLONG mt; char h[41]; } CacheEnt;
static CacheEnt *g_cache; static int g_ncache;

static void path_of(const char *rel, wchar_t *out) {
    wchar_t w[400]; MultiByteToWideChar(CP_UTF8, 0, rel, -1, w, 400);
    for (wchar_t *c = w; *c; c++) if (*c == L'/') *c = L'\\';
    swprintf(out, MAX_PATH * 2, L"%ls\\%ls", g_game, w);
}
static void make_dirs(const wchar_t *file) {
    wchar_t d[MAX_PATH * 2]; wcscpy(d, file);
    for (wchar_t *c = d + 3; *c; c++) if (*c == L'\\') { *c = 0; CreateDirectoryW(d, NULL); *c = L'\\'; }
}
static int file_info(const wchar_t *path, LONGLONG *size, LONGLONG *mtime) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return 0;
    *size = ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    *mtime = ((LONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static void load_cache(void) {
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\files.cache", g_base);
    FILE *f = _wfopen(path, L"r"); if (!f) return;
    char line[600];
    g_cache = calloc(4096, sizeof(CacheEnt));
    while (g_ncache < 4096 && fgets(line, sizeof line, f)) {
        CacheEnt *c = &g_cache[g_ncache];
        if (sscanf(line, "%40[0-9a-f]|%lld|%lld|%399[^\r\n]", c->h, &c->s, &c->mt, c->p) == 4) g_ncache++;
    }
    fclose(f);
}
static void save_cache(void) {
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\files.cache", g_base);
    FILE *f = _wfopen(path, L"w"); if (!f) return;
    for (int i = 0; i < g_nfiles; i++) {
        wchar_t fp[MAX_PATH * 2]; LONGLONG s, mt;
        path_of(g_files[i].p, fp);
        if (!g_files[i].need && file_info(fp, &s, &mt)) fprintf(f, "%s|%lld|%lld|%s\n", g_files[i].h, s, mt, g_files[i].p);
    }
    fclose(f);
}

/* SHA-1 of a file (with progress for big files) */
static int sha1_file(const wchar_t *path, char out[41], int show) {
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h; unsigned char dig[20];
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0)) { CloseHandle(f); return 0; }
    BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    static unsigned char buf[1 << 20]; DWORD got; LONGLONG tot = 0, size, mt;
    file_info(path, &size, &mt);
    while (ReadFile(f, buf, sizeof buf, &got, NULL) && got) {
        BCryptHashData(h, buf, got, 0);
        tot += got;
        if (show && size > (64 << 20) && (tot & ((64 << 20) - 1)) < got)
            set_status(-1, L"Checking game files... %d%%", (int)(tot * 100 / size));
    }
    BCryptFinishHash(h, dig, 20, 0);
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(f);
    for (int i = 0; i < 20; i++) sprintf(out + i * 2, "%02x", dig[i]);
    return 1;
}

static int parse_manifest(const char *js) {
    double v;
    if (!json_num(js, "version", &v)) return 0;
    g_version = (int)v;
    int cap = 256; g_nfiles = 0;
    free(g_files); g_files = calloc(cap, sizeof(FileEnt));
    const char *p = js;
    while ((p = strstr(p, "{\"p\": \""))) {
        if (g_nfiles == cap) { cap *= 2; g_files = realloc(g_files, cap * sizeof(FileEnt)); }
        FileEnt *e = &g_files[g_nfiles]; memset(e, 0, sizeof *e);
        p += 7;
        int n = 0; while (*p && *p != '"' && n < 399) e->p[n++] = *p++;
        e->p[n] = 0;
        if (strstr(e->p, "..") || strchr(e->p, ':') || e->p[0] == '/' || e->p[0] == '\\') return 0;   /* stay inside the game folder */
        if (!json_num(p, "s", &v)) return 0;
        e->s = (LONGLONG)v;
        const char *hp = strstr(p, "\"h\": \"");
        if (!hp) return 0;
        memcpy(e->h, hp + 6, 40); e->h[40] = 0;
        p = hp + 46;
        g_nfiles++;
    }
    return g_nfiles > 0;
}

/* mark which files must be downloaded; returns bytes needed */
static LONGLONG check_files(void) {
    LONGLONG need = 0;
    for (int i = 0; i < g_nfiles; i++) {
        FileEnt *e = &g_files[i];
        wchar_t fp[MAX_PATH * 2]; LONGLONG s, mt;
        path_of(e->p, fp);
        e->need = 1;
        if (file_info(fp, &s, &mt) && s == e->s) {
            const char *known = NULL;
            for (int k = 0; k < g_ncache; k++)
                if (g_cache[k].s == s && g_cache[k].mt == mt && !strcmp(g_cache[k].p, e->p)) { known = g_cache[k].h; break; }
            char h[41];
            if (known) e->need = strcmp(known, e->h) != 0;
            else if (sha1_file(fp, h, 1)) e->need = strcmp(h, e->h) != 0;
        }
        if (e->need) need += e->s;
    }
    return need;
}

static int download_file(FileEnt *e) {
    wchar_t fp[MAX_PATH * 2], part[MAX_PATH * 2], url[1200], wrel[400];
    path_of(e->p, fp);
    swprintf(part, MAX_PATH * 2, L"%ls.part", fp);
    make_dirs(fp);
    /* URL-encode the path */
    char enc[1200]; int n = 0;
    for (const unsigned char *c = (const unsigned char *)e->p; *c && n < 1180; c++) {
        if (isalnum(*c) || strchr("-_.~/", *c)) enc[n++] = *c;
        else n += sprintf(enc + n, "%%%02X", *c);
    }
    enc[n] = 0;
    MultiByteToWideChar(CP_UTF8, 0, enc, -1, wrel, 400);
    swprintf(url, 1200, L"/launcher/file?p=%ls", wrel);

    LONGLONG have = 0, mt;
    if (!file_info(part, &have, &mt)) have = 0;
    if (have > e->s) { DeleteFileW(part); have = 0; }
    g_done += have;
    if (have < e->s) {
        HINTERNET r = open_req(url, have);
        if (!r) return 0;
        DWORD code = status_code(r);
        if (code == 200 && have) { g_done -= have; have = 0; }          /* server ignored the resume */
        else if (code != 206 && code != 200) { WinHttpCloseHandle(r); return 0; }
        HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, have ? OPEN_ALWAYS : CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(r); return 0; }
        LARGE_INTEGER z; z.QuadPart = have; SetFilePointerEx(f, z, NULL, FILE_BEGIN); SetEndOfFile(f);
        static char buf[1 << 18]; DWORD got, wr; int ok = 1;
        for (;;) {
            if (!WinHttpReadData(r, buf, sizeof buf, &got)) { ok = 0; break; }
            if (!got) break;
            if (!WriteFile(f, buf, got, &wr, NULL) || wr != got) { ok = 0; break; }
            have += got; g_done += got;
            PostMessage(g_wnd, WM_REFRESH, 0, 0);
        }
        CloseHandle(f); WinHttpCloseHandle(r);
        if (!ok || have != e->s) return 0;
    }
    char h[41];
    if (!sha1_file(part, h, 0) || strcmp(h, e->h)) { DeleteFileW(part); return 0; }
    SetFileAttributesW(fp, FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(part, fp, MOVEFILE_REPLACE_EXISTING)) return 0;
    e->need = 0;
    return 1;
}

static void save_state(void) {
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\installed.txt", g_base);
    FILE *f = _wfopen(path, L"w"); if (f) { fprintf(f, "version=%d\n", g_version); fclose(f); }
}


/* ------------------------------------------------------------ game process watch */
static int game_running(void);
static void start_monitor(void);
/* ------------------------------------------------------------ worker */
static volatile int g_go;      /* user pressed Install/Update/Retry */
static int g_auto_retry;

/* wait for a click, or (auto > 0) retry by itself after that many seconds */
static void wait_click(int auto_secs) {
    for (int t = 0; !g_go; t++) {
        Sleep(100);
        if (auto_secs > 0 && t >= auto_secs * 10) break;
    }
    g_go = 0;
}

static DWORD WINAPI worker(LPVOID u) {
    (void)u;
    for (;;) {
        set_status(ST_CONNECTING, L"Connecting to the server...");
        DWORD code = 0;
        char *info = http_get(L"/launcher/info", &code);
        double ver = 0;
        if (!info || code != 200) {
            free(info);
            g_online = 0;
            set_status(ST_OFFLINE, L"Can't reach the server. Check your internet connection, then press Retry.");
            wait_click(30);
            continue;
        }
        g_online = 1;
        if (!json_num(info, "version", &ver)) {
            /* the server answered, but it doesn't have the launcher part yet */
            free(info);
            set_status(ST_OFFLINE, L"The server isn't set up for the launcher yet. The server owner needs to install the latest server update.");
            wait_click(60);
            continue;
        }
        double lob = 0, pl = 0;
        json_num(info, "lobbies", &lob); json_num(info, "players", &pl);
        wchar_t *notes = json_wstr(info, "notes");
        EnterCriticalSection(&g_cs);
        g_lobbies = (int)lob; g_players = (int)pl;
        free(g_notes); g_notes = notes;
        LeaveCriticalSection(&g_cs);
        free(info);
        PostMessage(g_wnd, WM_REFRESH, 1, 0);

        char *man = http_get(L"/launcher/manifest", &code);
        if (!man || code != 200 || !parse_manifest(man)) {
            free(man);
            if (code == 503)
                set_status(ST_WAITING, L"The server is preparing the game files (the first start after an update can take a few minutes). Retrying automatically...");
            else
                set_status(ST_OFFLINE, L"The server sent an unexpected answer. Press Retry.");
            wait_click(code == 503 ? 15 : 0);
            continue;
        }
        free(man);

        set_status(ST_CHECKING, L"Checking game files...");
        LONGLONG need = check_files();
        if (!need) {
            save_state(); save_cache();
            set_status(ST_READY, L"Up to date \x2022 version %d", g_version);
            if (game_running()) start_monitor();
            /* keep the numbers fresh, and notice a new update while the launcher is open (like Steam) */
            for (int t = 0;; t++) {
                Sleep(20000);
                char *i2 = http_get(L"/launcher/info", &code);
                double nv = 0;
                if (i2 && code == 200 && json_num(i2, "lobbies", &lob) && json_num(i2, "players", &pl)) {
                    g_lobbies = (int)lob; g_players = (int)pl; g_online = 1;
                    PostMessage(g_wnd, WM_REFRESH, 0, 0);
                }
                int newer = i2 && code == 200 && json_num(i2, "version", &nv) && (int)nv != g_version;
                free(i2);
                if (newer && g_state == ST_READY) break;     /* re-check files -> UPDATE button */
            }
            continue;
        }
        int cnt = 0; for (int i = 0; i < g_nfiles; i++) cnt += g_files[i].need;
        g_total = need; g_done = 0;
        set_status(ST_NEED, g_installed_once ? L"Update available \x2022 version %d \x2022 %d file(s), %.1f MB"
                                             : L"Ready to install \x2022 version %d \x2022 %d file(s), %.2f GB",
                   g_version, cnt, g_installed_once ? need / 1048576.0 : need / 1073741824.0);
        if (g_auto_retry) g_go = 0; else wait_click(0);   /* continuing an update by itself */

        ULARGE_INTEGER freeb;
        if (GetDiskFreeSpaceExW(g_base, &freeb, NULL, NULL) && (LONGLONG)freeb.QuadPart < need + (256LL << 20)) {
            set_status(ST_ERROR, L"Not enough disk space: %.1f GB free space needed. Free some space and press Retry.", need / 1073741824.0);
            wait_click(0);
            continue;
        }
        set_status(ST_DOWNLOADING, L"Downloading...");
        int ok = 1;
        for (int i = 0; i < g_nfiles && ok; i++) {
            if (!g_files[i].need) continue;
            const char *nm = strrchr(g_files[i].p, '/'); nm = nm ? nm + 1 : g_files[i].p;
            wchar_t wn[200]; MultiByteToWideChar(CP_UTF8, 0, nm, -1, wn, 200);
            set_status(-1, L"Downloading %ls", wn);
            LONGLONG before = g_done;
            int tries = 0;
            while (!(ok = download_file(&g_files[i])) && ++tries < 3) { g_done = before; Sleep(2000); }
        }
        save_cache();
        free(g_cache); g_cache = NULL; g_ncache = 0; load_cache();   /* remember what was just verified */
        if (!ok) {
            /* often the owner just published something newer: fetch the new file list and carry on by itself */
            if (++g_auto_retry <= 2) { Sleep(1500); continue; }
            set_status(ST_ERROR, L"Download interrupted. Press Retry to continue where it stopped.");
            wait_click(0);
            continue;
        }
        g_auto_retry = 0;
        g_installed_once = 1;
        save_state();
        /* loop: re-check everything, then show Play */
    }
}

/* ------------------------------------------------------------ Play */
static int steam_running(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof pe }; int found = 0;
    if (s == INVALID_HANDLE_VALUE) return 1;
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe))
        if (!_wcsicmp(pe.szExeFile, L"steam.exe")) { found = 1; break; }
    CloseHandle(s);
    return found;
}
static int proc_exists(const wchar_t *name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof pe }; int found = 0;
    if (s == INVALID_HANDLE_VALUE) return 0;
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe))
        if (!_wcsicmp(pe.szExeFile, name)) { found = 1; break; }
    CloseHandle(s);
    return found;
}
static int game_running(void) {
    return proc_exists(L"ProSoccerOnline-Win64-Shipping.exe") || proc_exists(L"ProSoccerOnline.exe");
}
static void steam_refresh(void);
static volatile int g_monitoring;
/* RUNNING while the game is open; back to PLAY when it closes */
static DWORD WINAPI monitor(LPVOID u) {
    (void)u;
    int seen = 0;
    set_status(ST_RUNNING, L"Pro Soccer Online is running. Have fun!");
    for (int t = 0;; t++) {
        Sleep(1500);
        int r = game_running();
        if (r) seen = 1;
        if (!r && (seen || t > 40)) break;      /* closed (or never started after 60 s) */
    }
    g_monitoring = 0;
    PostMessage(g_wnd, WM_APP + 0x77, 0, 0);      /* game closed: show the launcher again */
    steam_refresh();                              /* new play time from Steam */
    set_status(ST_READY, L"Up to date \x2022 version %d", g_version);
    return 0;
}
static void start_monitor(void) {
    if (g_monitoring) return;
    g_monitoring = 1;
    CloseHandle(CreateThread(NULL, 0, monitor, NULL, 0, NULL));
}
static void play(void) {
    if (game_running()) { ShowWindow(g_wnd, SW_HIDE); start_monitor(); return; }
    if (!steam_running()) {
        ShellExecuteW(NULL, L"open", L"steam://open/main", NULL, NULL, SW_SHOWNORMAL);
        MessageBoxW(g_wnd, L"Steam has to be open and logged in.\n\nSteam is starting now - when it's ready, press Play again.",
                    L"PSO UE4 Launcher", MB_ICONINFORMATION);
        return;
    }
    wchar_t exe[MAX_PATH * 2]; swprintf(exe, MAX_PATH * 2, L"%ls\\ProSoccerOnline.exe", g_game);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, g_game, &si, &pi)) {
        MessageBoxW(g_wnd, L"Could not start the game. Restart the launcher to repair the files.", L"PSO UE4 Launcher", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    ShowWindow(g_wnd, SW_HIDE);                    /* game started: the launcher gets out of the way ... */
    start_monitor();                               /* ... and comes back when the game is closed */
}

/* launcher build number: the game add-on only replaces a launcher with a higher one */
__attribute__((used)) static const char g_build_tag[] = "PSOLAUNCHER_BUILD=5;";
#include "steamstats.h"

/* ------------------------------------------------------------ GDI+ flat API (antialiased drawing, images) */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
typedef struct { INT X, Y; } GpPointI;
int WINAPI GdiplusStartup(ULONG_PTR *, const GpStartupInput *, void *);
int WINAPI GdipCreateBitmapFromStream(IStream *, void **);
int WINAPI GdipDisposeImage(void *);
int WINAPI GdipGetImageWidth(void *, UINT *);
int WINAPI GdipGetImageHeight(void *, UINT *);
int WINAPI GdipCreateFromHDC(HDC, void **);
int WINAPI GdipDeleteGraphics(void *);
int WINAPI GdipSetInterpolationMode(void *, int);
int WINAPI GdipSetSmoothingMode(void *, int);
int WINAPI GdipSetPixelOffsetMode(void *, int);
int WINAPI GdipDrawImageRectRectI(void *, void *, int, int, int, int, int, int, int, int, int, void *, void *, void *);
int WINAPI GdipCreatePath(int, void **);
int WINAPI GdipDeletePath(void *);
int WINAPI GdipAddPathArcI(void *, INT, INT, INT, INT, float, float);
int WINAPI GdipAddPathEllipseI(void *, INT, INT, INT, INT);
int WINAPI GdipClosePathFigure(void *);
int WINAPI GdipCreateSolidFill(DWORD, void **);
int WINAPI GdipCreateLineBrushI(const GpPointI *, const GpPointI *, DWORD, DWORD, int, void **);
int WINAPI GdipDeleteBrush(void *);
int WINAPI GdipFillPath(void *, void *, void *);
int WINAPI GdipFillRectangleI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipFillPolygonI(void *, void *, const GpPointI *, INT, int);
int WINAPI GdipSetClipPath(void *, void *, int);
int WINAPI GdipResetClip(void *);
int WINAPI GdipCreatePen1(DWORD, float, int, void **);
int WINAPI GdipDeletePen(void *);
int WINAPI GdipDrawPath(void *, void *, void *);
int WINAPI GdipDrawLineI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipDrawEllipseI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipFillEllipseI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipDrawArc(void *, void *, float, float, float, float, float, float);
int WINAPI GdipSetPenStartCap(void *, int);
int WINAPI GdipSetPenEndCap(void *, int);
int WINAPI GdipFillPolygon(void *, void *, const void *, INT, int);
IStream *WINAPI SHCreateMemStream(const BYTE *, UINT);
#include "art.h"

#define ARGB(a, r, g, b) ((DWORD)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
static void *g_banner, *g_avatar, *g_backdrop, *g_logo;
static CRITICAL_SECTION g_imgcs;

static void *load_image_mem(const void *data, UINT len) {
    IStream *s = SHCreateMemStream((const BYTE *)data, len); void *img = NULL;
    if (s) { GdipCreateBitmapFromStream(s, &img); s->lpVtbl->Release(s); }
    return img;
}
static void *load_image_file(const wchar_t *path) {
    long n; char *d = read_all(path, &n); void *img = NULL;
    if (d) { img = load_image_mem(d, (UINT)n); free(d); }
    return img;
}
static void draw_img(void *G, void *img, int x, int y, int w, int h) {   /* cover: crop to fill */
    if (!img || w <= 0 || h <= 0) return;
    UINT iw = 0, ih = 0; GdipGetImageWidth(img, &iw); GdipGetImageHeight(img, &ih);
    if (!iw || !ih) return;
    double sx = (double)w / iw, sy = (double)h / ih, s = sx > sy ? sx : sy;
    int cw = (int)(w / s), ch = (int)(h / s);
    if (cw > (int)iw) cw = iw; if (ch > (int)ih) ch = ih;
    GdipDrawImageRectRectI(G, img, x, y, w, h, (iw - cw) / 2, (ih - ch) / 2, cw, ch, 2, NULL, NULL, NULL);
}
static void *rr_path(int x, int y, int w, int h, int r) {
    void *p; GdipCreatePath(0, &p);
    if (r < 1) r = 1; int d = r * 2;
    GdipAddPathArcI(p, x, y, d, d, 180, 90);
    GdipAddPathArcI(p, x + w - d - 1, y, d, d, 270, 90);
    GdipAddPathArcI(p, x + w - d - 1, y + h - d - 1, d, d, 0, 90);
    GdipAddPathArcI(p, x, y + h - d - 1, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}
static void rr_fill(void *G, int x, int y, int w, int h, int r, DWORD c) {
    void *p = rr_path(x, y, w, h, r), *b; GdipCreateSolidFill(c, &b);
    GdipFillPath(G, b, p); GdipDeleteBrush(b); GdipDeletePath(p);
}
static void rr_grad(void *G, int x, int y, int w, int h, int r, DWORD top, DWORD bot) {
    void *p = rr_path(x, y, w, h, r), *b;
    GpPointI a = { x, y - 1 }, z = { x, y + h + 1 };
    GdipCreateLineBrushI(&a, &z, top, bot, 0, &b);
    GdipFillPath(G, b, p); GdipDeleteBrush(b); GdipDeletePath(p);
}
static void rr_line(void *G, int x, int y, int w, int h, int r, DWORD c) {
    void *p = rr_path(x, y, w, h, r), *pen; GdipCreatePen1(c, 1.0f, 2, &pen);
    GdipDrawPath(G, pen, p); GdipDeletePen(pen); GdipDeletePath(p);
}
static void vgrad(void *G, int x, int y, int w, int h, DWORD top, DWORD bot) {
    void *b; GpPointI a = { x, y - 1 }, z = { x, y + h + 1 };
    GdipCreateLineBrushI(&a, &z, top, bot, 0, &b);
    GdipFillRectangleI(G, b, x, y, w, h); GdipDeleteBrush(b);
}

/* ------------------------------------------------------------ DPI + fonts */
static int g_dpi = 96;
#define S(v) MulDiv((v), g_dpi, 96)
static HFONT f_cap, f_btn, f_btn2, f_lbl, f_val, f_text, f_small, f_h2, f_big, f_valb, f_title;
static HFONT mkfont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
static void make_fonts(void) {
    HFONT *all[] = { &f_cap, &f_btn, &f_btn2, &f_lbl, &f_val, &f_text, &f_small, &f_h2, &f_big, &f_valb, &f_title };
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) if (*all[i]) DeleteObject(*all[i]);
    f_cap = mkfont(12, 600); f_btn = mkfont(20, 700); f_btn2 = mkfont(15, 700); f_lbl = mkfont(11, 700);
    f_val = mkfont(17, 600); f_valb = mkfont(15, 600); f_text = mkfont(14, 400); f_small = mkfont(12, 400);
    f_h2 = mkfont(18, 700); f_big = mkfont(28, 700); f_title = mkfont(14, 700);
}

/* ------------------------------------------------------------ look */
#define C_BG    RGB(15, 20, 27)
#define C_TEXT  RGB(240, 244, 248)
#define C_DIM   RGB(176, 188, 202)
#define C_LBL   RGB(150, 168, 190)
#define C_GREEN RGB(102, 214, 92)
#define C_RED   RGB(240, 92, 92)
#define C_BLUE  RGB(84, 178, 255)

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, UINT fl) {
    RECT r = { x, y, x + w, y + h }; SelectObject(dc, f); SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
}
static int text_w(HDC dc, HFONT f, const wchar_t *s) {
    SIZE sz; SelectObject(dc, f); GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz); return sz.cx;
}
static void fmt_hours(long long minutes, wchar_t *out, int cap) {
    double h = minutes / 60.0; long long whole = (long long)h; int dec = (int)((h - whole) * 10 + 0.5);
    if (dec == 10) { whole++; dec = 0; }
    wchar_t num[32]; swprintf(num, 32, L"%lld", whole);
    wchar_t grp[48]; int n = (int)wcslen(num), j = 0;
    for (int i = 0; i < n; i++) { grp[j++] = num[i]; if ((n - i - 1) % 3 == 0 && i != n - 1) grp[j++] = L','; }
    grp[j] = 0;
    swprintf(out, cap, L"%ls.%d hours", grp, dec);
}
static void fmt_last(long long t, wchar_t *out, int cap) {
    if (t <= 0) { wcscpy(out, L"Never"); return; }
    __time64_t now = _time64(NULL), tt = t; struct tm a, b;
    _localtime64_s(&a, &tt); _localtime64_s(&b, &now);
    static const wchar_t *mon[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
    __time64_t mid = now - (b.tm_hour * 3600 + b.tm_min * 60 + b.tm_sec);
    if (tt >= mid) wcscpy(out, L"Today");
    else if (tt >= mid - 86400) wcscpy(out, L"Yesterday");
    else if (a.tm_year == b.tm_year) swprintf(out, cap, L"%ls %d", mon[a.tm_mon], a.tm_mday);
    else swprintf(out, cap, L"%ls %d, %d", mon[a.tm_mon], a.tm_mday, a.tm_year + 1900);
}

/* ------------------------------------------------------------ layout */
typedef struct { int W, H, M, tb; RECT hero, act, play, bmin, bclose, notes, server, title; } Layout;
static Layout L;
static void calc_layout(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    L.W = rc.right; L.H = rc.bottom; L.M = S(16); L.tb = S(46);
    int heroH = (L.W - 2 * L.M) * 10 / 38;                         /* wide banner, like Steam */
    int maxH = L.H - L.tb - S(12) - S(84) - S(12) - S(178) - L.M;   /* leave room for the cards */
    if (heroH > maxH) heroH = maxH; if (heroH > S(380)) heroH = S(380); if (heroH < S(170)) heroH = S(170);
    SetRect(&L.hero, L.M, L.tb, L.W - L.M, L.tb + heroH);
    SetRect(&L.act, L.M, L.hero.bottom + S(12), L.W - L.M, L.hero.bottom + S(12) + S(84));
    SetRect(&L.play, L.act.left + S(18), L.act.top + S(18), L.act.left + S(18) + S(196), L.act.top + S(18) + S(48));
    SetRect(&L.bclose, L.W - S(44), S(9), L.W - S(10), S(37));
    SetRect(&L.bmin, L.W - S(82), S(9), L.W - S(48), S(37));
    SetRect(&L.title, L.M, S(9), L.M + S(250), S(37));
    int cy = L.act.bottom + S(12), rw = S(330);
    SetRect(&L.notes, L.M, cy, L.W - L.M - rw - S(12), L.H - L.M);
    SetRect(&L.server, L.W - L.M - rw, cy, L.W - L.M, L.H - L.M);
}
static int in_rect(const RECT *r, int x, int y) { return x >= r->left && x < r->right && y >= r->top && y < r->bottom; }

/* ------------------------------------------------------------ interaction state */
enum { HOT_NONE, HOT_PLAY, HOT_MIN, HOT_CLOSE };
static int g_hot, g_pressed, g_scroll, g_content_h;
static double g_speed;
static LONGLONG g_last_done;
static float g_spin;                 /* update-arrow angle (degrees) */
static int spinning(void) { return g_state == ST_DOWNLOADING || g_state == ST_CHECKING || g_state == ST_CONNECTING || g_state == ST_WAITING; }

/* circular "refresh" arrow (like Steam's update icon), rotated by deg, centred at cx,cy */
typedef struct { float X, Y; } GpPointF;
static void draw_update_arrow(void *G, float cx, float cy, float r, float deg, DWORD col) {
    void *pen; GdipCreatePen1(col, r * 0.30f, 2, &pen);
    GdipSetPenStartCap(pen, 2); GdipSetPenEndCap(pen, 0);
    float start = deg + 40.0f, sweep = 270.0f;
    GdipDrawArc(G, pen, cx - r, cy - r, 2 * r, 2 * r, start, sweep);
    GdipDeletePen(pen);
    float a = (start + sweep) * 3.14159265f / 180.0f;                 /* arrow head at the end of the arc */
    float ex = cx + r * cosf(a), ey = cy + r * sinf(a);
    float tx = -sinf(a), ty = cosf(a);                                  /* direction of travel */
    float nx = cosf(a), ny = sinf(a);                                   /* outward */
    float hl = r * 0.62f, hw = r * 0.48f;
    GpPointF tri[3] = { { ex + tx * hl, ey + ty * hl }, { ex + nx * hw - tx * hl * 0.15f, ey + ny * hw - ty * hl * 0.15f },
                        { ex - nx * hw - tx * hl * 0.15f, ey - ny * hw - ty * hl * 0.15f } };
    void *br; GdipCreateSolidFill(col, &br); GdipFillPolygon(G, br, tri, 3, 0); GdipDeleteBrush(br);
}

static const wchar_t *btn_text(void) {
    switch (g_state) {
    case ST_CONNECTING: return L"CONNECTING";
    case ST_CHECKING:   return L"CHECKING";
    case ST_NEED:       return g_installed_once ? L"UPDATE" : L"INSTALL";
    case ST_DOWNLOADING:return L"DOWNLOADING";
    case ST_READY:      return L"PLAY";
    case ST_RUNNING:    return L"RUNNING";
    case ST_WAITING:    return L"WAITING";
    default:            return L"RETRY";
    }
}
static int btn_enabled(void) { return g_state == ST_NEED || g_state == ST_READY || g_state == ST_OFFLINE || g_state == ST_ERROR || g_state == ST_WAITING; }

/* glass panel: translucent fill, top highlight, soft border, drop shadow */
static void glass(void *G, RECT r, int rad) {
    int w = r.right - r.left, h = r.bottom - r.top;
    for (int i = 4; i >= 1; i--) rr_fill(G, r.left - S(i), r.top - S(i) + S(4), w + S(2 * i), h + S(2 * i), rad + S(i), ARGB(14, 0, 0, 0));
    rr_grad(G, r.left, r.top, w, h, rad, ARGB(226, 40, 54, 72), ARGB(234, 30, 41, 56));
    rr_grad(G, r.left, r.top, w, S(44), rad, ARGB(26, 255, 255, 255), ARGB(0, 255, 255, 255));
    rr_line(G, r.left, r.top, w, h, rad, ARGB(48, 255, 255, 255));
}

/* ------------------------------------------------------------ pieces */
static void draw_title(HDC m, void *G) {
    RECT t = L.title; int h = t.bottom - t.top;
    int w = (h - S(8)) + S(12) + text_w(m, f_title, L"PSO UE4 Launcher") + S(18);   /* fit the text */
    t.right = t.left + w;
    rr_fill(G, t.left, t.top, w, h, h / 2, ARGB(150, 24, 34, 46));
    rr_line(G, t.left, t.top, w, h, h / 2, ARGB(60, 255, 255, 255));
    int lg = h - S(8);
    EnterCriticalSection(&g_imgcs);
    if (g_logo) GdipDrawImageRectRectI(G, g_logo, t.left + S(6), t.top + S(4), lg, lg, 0, 0, 128, 128, 2, NULL, NULL, NULL);
    LeaveCriticalSection(&g_imgcs);
    text(m, f_title, RGB(255, 255, 255), t.left + S(12) + lg, t.top, w - lg - S(16), h, L"PSO UE4 Launcher", DT_SINGLELINE | DT_VCENTER);
    int tw = text_w(m, f_title, L"PSO UE4 Launcher");
    /* shrink the pill to the text */
    (void)tw;
}

static void draw_window_buttons(void *G) {
    RECT bs[2] = { L.bmin, L.bclose };
    for (int i = 0; i < 2; i++) {
        RECT r = bs[i]; int w = r.right - r.left, h = r.bottom - r.top, hot = g_hot == (i ? HOT_CLOSE : HOT_MIN);
        rr_fill(G, r.left, r.top, w, h, S(8), hot ? (i ? ARGB(235, 232, 40, 52) : ARGB(90, 255, 255, 255)) : ARGB(140, 24, 34, 46));
        rr_line(G, r.left, r.top, w, h, S(8), ARGB(50, 255, 255, 255));
        void *pen; GdipCreatePen1(ARGB(245, 255, 255, 255), (float)S(2) * 0.75f, 2, &pen);
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, k = S(5);
        if (!i) GdipDrawLineI(G, pen, cx - k, cy, cx + k, cy);
        else { GdipDrawLineI(G, pen, cx - k, cy - k, cx + k, cy + k); GdipDrawLineI(G, pen, cx - k, cy + k, cx + k, cy - k); }
        GdipDeletePen(pen);
    }
}

static void draw_play_button(HDC m, void *G) {
    RECT r = L.play; int w = r.right - r.left, h = r.bottom - r.top, en = btn_enabled();
    int hot = en && g_hot == HOT_PLAY, down = hot && g_pressed == HOT_PLAY;
    int running = g_state == ST_RUNNING || g_state == ST_DOWNLOADING || g_state == ST_CHECKING;   /* busy = blue, like Steam */
    DWORD top, bot, glow;
    if (running) { top = ARGB(255, 46, 128, 214); bot = ARGB(255, 28, 92, 170); glow = ARGB(60, 50, 140, 255); }
    else if (!en) { top = ARGB(255, 62, 72, 86); bot = ARGB(255, 48, 56, 68); glow = 0; }
    else if (g_state == ST_READY) {
        top = down ? ARGB(255, 70, 170, 60) : hot ? ARGB(255, 132, 236, 106) : ARGB(255, 110, 220, 90);
        bot = down ? ARGB(255, 44, 132, 46) : hot ? ARGB(255, 74, 190, 70) : ARGB(255, 56, 168, 58); glow = ARGB(hot ? 110 : 70, 90, 220, 80);
    } else {
        top = down ? ARGB(255, 30, 120, 210) : hot ? ARGB(255, 100, 192, 255) : ARGB(255, 72, 172, 255);
        bot = down ? ARGB(255, 18, 92, 180) : hot ? ARGB(255, 44, 146, 245) : ARGB(255, 28, 124, 230); glow = ARGB(hot ? 110 : 70, 50, 150, 255);
    }
    if (glow) for (int i = 4; i >= 1; i--) rr_fill(G, r.left - S(i * 2), r.top - S(i * 2) + S(3), w + S(i * 4), h + S(i * 4), S(10 + i * 2), (glow & 0x00FFFFFF) | ((DWORD)((glow >> 24) / (i + 1)) << 24));
    rr_grad(G, r.left, r.top, w, h, S(10), top, bot);
    rr_grad(G, r.left + 1, r.top + 1, w - 2, h / 2, S(10), ARGB(60, 255, 255, 255), ARGB(0, 255, 255, 255));   /* gloss */
    rr_line(G, r.left, r.top, w, h, S(10), ARGB(70, 255, 255, 255));
    const wchar_t *t = btn_text();
    HFONT f = wcslen(t) > 8 ? f_btn2 : f_btn;
    COLORREF tc = (en || running) ? RGB(255, 255, 255) : RGB(170, 178, 190);
    if (g_state == ST_READY || g_state == ST_RUNNING) {
        int tw = text_w(m, f, t), iw = S(g_state == ST_RUNNING ? 18 : 16), gap = S(12), x0 = r.left + (w - (iw + gap + tw)) / 2, cy = r.top + h / 2;
        void *b; GdipCreateSolidFill(ARGB(255, 255, 255, 255), &b);
        if (g_state == ST_RUNNING) {         /* two bars = in game */
            GdipFillRectangleI(G, b, x0, cy - S(9), S(6), S(18)); GdipFillRectangleI(G, b, x0 + S(11), cy - S(9), S(6), S(18));
        } else {
            GpPointI tri[3] = { { x0, cy - S(10) }, { x0, cy + S(10) }, { x0 + iw, cy } };
            GdipFillPolygonI(G, b, tri, 3, 0);
        }
        GdipDeleteBrush(b);
        text(m, f, tc, x0 + iw + gap, r.top, tw + S(4), h, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_state == ST_NEED || spinning() || g_state == ST_ERROR || g_state == ST_OFFLINE) {
        /* update arrow + text (spins while working) */
        int tw = text_w(m, f, t), iw = S(20), gap = S(10), x0 = r.left + (w - (iw + gap + tw)) / 2, cy = r.top + h / 2;
        draw_update_arrow(G, (float)(x0 + iw / 2), (float)cy, (float)S(8), spinning() ? g_spin : 0.0f, en || running ? ARGB(255, 255, 255, 255) : ARGB(255, 175, 185, 198));
        text(m, f, tc, x0 + iw + gap, r.top, tw + S(4), h, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        text(m, f, tc, r.left, r.top, w, h, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void icon_clock(void *G, int x, int y) {
    void *pen; GdipCreatePen1(ARGB(255, 150, 168, 190), (float)S(2), 2, &pen);
    GdipDrawEllipseI(G, pen, x, y, S(26), S(26));
    GdipDrawLineI(G, pen, x + S(13), y + S(7), x + S(13), y + S(13));
    GdipDrawLineI(G, pen, x + S(13), y + S(13), x + S(18), y + S(16));
    GdipDeletePen(pen);
}
static void icon_calendar(void *G, int x, int y) {
    void *pen; GdipCreatePen1(ARGB(255, 150, 168, 190), (float)S(2), 2, &pen);
    void *p = rr_path(x, y + S(3), S(26), S(23), S(4)); GdipDrawPath(G, pen, p); GdipDeletePath(p);
    GdipDrawLineI(G, pen, x, y + S(11), x + S(26), y + S(11));
    GdipDrawLineI(G, pen, x + S(7), y, x + S(7), y + S(6)); GdipDrawLineI(G, pen, x + S(19), y, x + S(19), y + S(6));
    GdipDeletePen(pen);
}
static void vsep(void *G, int x, int y, int h) {
    void *b; GdipCreateSolidFill(ARGB(40, 255, 255, 255), &b); GdipFillRectangleI(G, b, x, y, 1, h); GdipDeleteBrush(b);
}

static void draw_action(HDC m, void *G) {
    glass(G, L.act, S(14));
    draw_play_button(m, G);
    int ay = L.act.top - S(8), sx = L.play.right + S(30), right_limit = L.act.right - S(270);
    EnterCriticalSection(&g_cs);
    if (g_state == ST_DOWNLOADING || (g_state == ST_NEED && g_done > 0)) {
        double fr = g_total ? (double)g_done / g_total : 0; if (fr > 1) fr = 1;
        int pw = right_limit - sx;
        text(m, f_valb, C_TEXT, sx, ay + S(22), pw, S(24), g_status, DT_SINGLELINE | DT_END_ELLIPSIS);
        wchar_t sp[96] = L"";
        if (g_state == ST_DOWNLOADING && g_speed > 1) {
            double left = (g_total - g_done) / g_speed;
            if (left < 60) swprintf(sp, 96, L"%.1f MB/s  \x2022  %d sec left", g_speed / 1048576.0, (int)left + 1);
            else swprintf(sp, 96, L"%.1f MB/s  \x2022  %d min left", g_speed / 1048576.0, (int)(left / 60) + 1);
        }
        text(m, f_small, C_DIM, sx, ay + S(25), pw, S(20), sp, DT_SINGLELINE | DT_RIGHT);
        rr_fill(G, sx, ay + S(54), pw, S(10), S(5), ARGB(255, 50, 64, 82));
        int fw = (int)(pw * fr); if (fw > 0 && fw < S(10)) fw = S(10);
        if (fw > 0) {
            rr_grad(G, sx, ay + S(54), fw, S(10), S(5), ARGB(255, 110, 200, 255), ARGB(255, 36, 130, 236));
            rr_fill(G, sx, ay + S(52), fw, S(14), S(7), ARGB(30, 84, 178, 255));
        }
        wchar_t t[96]; swprintf(t, 96, L"%.2f / %.2f GB   %d%%", g_done / 1073741824.0, g_total / 1073741824.0, (int)(fr * 100));
        text(m, f_small, C_DIM, sx, ay + S(70), pw, S(20), t, DT_SINGLELINE);
    } else {
        wchar_t v[64]; int col = sx;
        vsep(G, col - S(15), L.act.top + S(18), S(48));
        icon_calendar(G, col, ay + S(36));
        text(m, f_lbl, C_LBL, col + S(40), ay + S(28), S(160), S(18), L"LAST PLAYED", DT_SINGLELINE);
        if (g_steam.ok) fmt_last(g_steam.last_played, v, 64); else wcscpy(v, L"\x2014");
        text(m, f_val, C_TEXT, col + S(40), ay + S(48), S(160), S(28), v, DT_SINGLELINE);
        col += S(40) + S(160);
        vsep(G, col - S(15), L.act.top + S(18), S(48));
        icon_clock(G, col, ay + S(36));
        text(m, f_lbl, C_LBL, col + S(40), ay + S(28), S(200), S(18), L"PLAY TIME", DT_SINGLELINE);
        if (g_steam.ok && g_steam.minutes >= 0) fmt_hours(g_steam.minutes, v, 64); else wcscpy(v, L"\x2014");
        text(m, f_val, C_TEXT, col + S(40), ay + S(48), S(220), S(28), v, DT_SINGLELINE);
    }
    LeaveCriticalSection(&g_cs);

    /* Steam account */
    int ax = L.act.right - S(250), avy = L.act.top + S(19), av = S(46);
    vsep(G, ax - S(20), L.act.top + S(18), S(48));
    if (g_steam.ok) {
        void *ring; GpPointI a = { ax, avy }, z = { ax, avy + av };
        GdipCreateLineBrushI(&a, &z, ARGB(255, 130, 236, 108), ARGB(255, 50, 168, 72), 0, &ring);
        GdipFillEllipseI(G, ring, ax - S(3), avy - S(3), av + S(6), av + S(6)); GdipDeleteBrush(ring);
        void *bg; GdipCreateSolidFill(ARGB(255, 34, 46, 62), &bg); GdipFillEllipseI(G, bg, ax - S(1), avy - S(1), av + S(2), av + S(2)); GdipDeleteBrush(bg);
        void *clip; GdipCreatePath(0, &clip); GdipAddPathEllipseI(clip, ax, avy, av, av);
        GdipSetClipPath(G, clip, 0);
        void *ph; GdipCreateSolidFill(ARGB(255, 52, 62, 76), &ph); GdipFillEllipseI(G, ph, ax, avy, av, av); GdipDeleteBrush(ph);
        EnterCriticalSection(&g_imgcs); draw_img(G, g_avatar, ax, avy, av, av); LeaveCriticalSection(&g_imgcs);
        GdipResetClip(G); GdipDeletePath(clip);
        text(m, f_valb, RGB(255, 255, 255), ax + av + S(14), avy + S(2), S(200), S(24), g_steam.name, DT_SINGLELINE | DT_END_ELLIPSIS);
        void *dot; GdipCreateSolidFill(ARGB(255, 110, 226, 96), &dot); GdipFillEllipseI(G, dot, ax + av + S(15), avy + S(33), S(8), S(8)); GdipDeleteBrush(dot);
        text(m, f_small, C_GREEN, ax + av + S(29), avy + S(27), S(190), S(20), g_state == ST_RUNNING ? L"In game" : L"Connected with Steam", DT_SINGLELINE);
    } else {
        text(m, f_valb, C_DIM, ax, avy + S(2), S(250), S(24), L"Steam not found", DT_SINGLELINE);
        text(m, f_small, C_DIM, ax, avy + S(28), S(250), S(20), L"Open Steam to see your stats", DT_SINGLELINE);
    }
}

/* patch notes: first line = title, "- " / "* " lines = bullets. measure=1 only returns height */
static int draw_notes_text(HDC m, int x, int y, int w, int measure) {
    EnterCriticalSection(&g_cs);
    const wchar_t *s = g_notes && g_notes[0] ? g_notes : L"No patch notes yet.";
    int cy = y, first = 1;
    while (*s) {
        const wchar_t *e = s; while (*e && *e != L'\r' && *e != L'\n') e++;
        int n = (int)(e - s);
        wchar_t line[2048]; if (n > 2047) n = 2047; wcsncpy(line, s, n); line[n] = 0;
        wchar_t *t = line; while (*t == L' ') t++;
        if (!*t) cy += S(10);
        else {
            int bullet = (t[0] == L'-' || t[0] == L'*' || t[0] == 0x2022) && t[1] == L' ';
            HFONT f = first ? f_h2 : f_text;
            int ix = bullet ? S(22) : 0;
            const wchar_t *body = bullet ? t + 2 : t;
            RECT r = { x + ix, cy, x + w, cy + 10000 };
            SelectObject(m, f);
            DrawTextW(m, body, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            int lh = r.bottom - r.top;
            if (!measure) {
                if (bullet) {      /* accent bullet */
                    void *G2; GdipCreateFromHDC(m, &G2); GdipSetSmoothingMode(G2, 4);
                    void *b; GdipCreateSolidFill(ARGB(255, 84, 178, 255), &b);
                    GdipFillEllipseI(G2, b, x + S(5), cy + lh / 2 - S(4), S(8), S(8)); GdipDeleteBrush(b); GdipDeleteGraphics(G2);
                }
                r.right = x + w; SetTextColor(m, first ? RGB(255, 255, 255) : C_TEXT);
                DrawTextW(m, body, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
            }
            cy += lh + (first ? S(12) : S(6));
            first = 0;
        }
        s = e; if (*s == L'\r') s++; if (*s == L'\n') s++;
    }
    LeaveCriticalSection(&g_cs);
    return cy - y;
}

static void card_header(HDC m, void *G, RECT r, const wchar_t *title, COLORREF accent) {
    void *b; GdipCreateSolidFill(ARGB(255, GetRValue(accent), GetGValue(accent), GetBValue(accent)), &b);
    GdipFillRectangleI(G, b, r.left + S(20), r.top + S(17), S(4), S(14)); GdipDeleteBrush(b);
    text(m, f_lbl, RGB(222, 232, 244), r.left + S(31), r.top + S(14), S(220), S(20), title, DT_SINGLELINE | DT_VCENTER);
}

static void draw_cards(HDC m, void *G) {
    RECT n = L.notes; int nw = n.right - n.left, nh = n.bottom - n.top;
    if (nh > S(70)) {
        glass(G, n, S(14));
        card_header(m, G, n, L"PATCH NOTES", C_BLUE);
        if (g_version) {
            wchar_t v[32]; swprintf(v, 32, L"VERSION %d", g_version);
            int tw = text_w(m, f_lbl, v) + S(22);
            rr_fill(G, n.right - S(22) - tw, n.top + S(12), tw, S(24), S(12), ARGB(60, 84, 178, 255));
            rr_line(G, n.right - S(22) - tw, n.top + S(12), tw, S(24), S(12), ARGB(90, 84, 178, 255));
            text(m, f_lbl, RGB(170, 214, 255), n.right - S(22) - tw, n.top + S(12), tw, S(24), v, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        void *b; GdipCreateSolidFill(ARGB(30, 255, 255, 255), &b); GdipFillRectangleI(G, b, n.left + S(20), n.top + S(48), nw - S(40), 1); GdipDeleteBrush(b);
        int tx = n.left + S(22), ty = n.top + S(60), tw = nw - S(52), th = nh - S(72);
        g_content_h = draw_notes_text(m, tx, 0, tw, 1);
        int maxs = g_content_h - th; if (maxs < 0) maxs = 0;
        if (g_scroll > maxs) g_scroll = maxs; if (g_scroll < 0) g_scroll = 0;
        SaveDC(m); IntersectClipRect(m, tx, ty, tx + tw, ty + th);
        draw_notes_text(m, tx, ty - g_scroll, tw, 0);
        RestoreDC(m, -1);
        if (maxs > 0) {
            int thumb = th * th / g_content_h; if (thumb < S(30)) thumb = S(30);
            int pos = ty + (th - thumb) * g_scroll / maxs;
            rr_fill(G, n.right - S(12), pos, S(4), thumb, S(2), ARGB(90, 255, 255, 255));
        }
    }
    RECT s = L.server; int sw = s.right - s.left, sh = s.bottom - s.top;
    if (sh > S(70)) {
        glass(G, s, S(14));
        int on = g_online && g_state != ST_OFFLINE;
        COLORREF pc = on ? C_GREEN : (g_state == ST_CONNECTING ? C_DIM : C_RED);
        card_header(m, G, s, L"SERVER", pc);
        const wchar_t *pill = on ? L"ONLINE" : (g_state == ST_CONNECTING ? L"CONNECTING" : L"OFFLINE");
        int pw = text_w(m, f_lbl, pill) + S(36);
        rr_fill(G, s.right - S(22) - pw, s.top + S(12), pw, S(24), S(12), on ? ARGB(55, 102, 214, 92) : ARGB(55, 240, 92, 92));
        rr_line(G, s.right - S(22) - pw, s.top + S(12), pw, S(24), S(12), on ? ARGB(110, 102, 214, 92) : ARGB(110, 240, 92, 92));
        void *dot; GdipCreateSolidFill(ARGB(255, GetRValue(pc), GetGValue(pc), GetBValue(pc)), &dot);
        GdipFillEllipseI(G, dot, s.right - S(22) - pw + S(12), s.top + S(20), S(8), S(8)); GdipDeleteBrush(dot);
        text(m, f_lbl, pc, s.right - S(22) - pw + S(25), s.top + S(12), pw - S(25), S(24), pill, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        void *b; GdipCreateSolidFill(ARGB(30, 255, 255, 255), &b); GdipFillRectangleI(G, b, s.left + S(20), s.top + S(48), sw - S(40), 1); GdipDeleteBrush(b);
        int tw2 = (sw - S(40) - S(10)) / 2, ty = s.top + S(62), th2 = S(74);
        const wchar_t *lbl[2] = { L"LOBBIES OPEN", L"IN MATCHES" };
        int val[2] = { g_lobbies, g_players };
        DWORD acc[2] = { ARGB(255, 84, 178, 255), ARGB(255, 110, 226, 96) };
        for (int i = 0; i < 2; i++) {
            int tx = s.left + S(20) + i * (tw2 + S(10));
            rr_grad(G, tx, ty, tw2, th2, S(12), ARGB(255, 52, 68, 89), ARGB(255, 42, 56, 74));
            rr_line(G, tx, ty, tw2, th2, S(12), ARGB(40, 255, 255, 255));
            void *ab; GdipCreateSolidFill(acc[i], &ab); GdipFillRectangleI(G, ab, tx + S(14), ty + S(14), S(18), S(3)); GdipDeleteBrush(ab);
            wchar_t v[16]; if (val[i] >= 0) swprintf(v, 16, L"%d", val[i]); else wcscpy(v, L"\x2014");
            text(m, f_big, RGB(255, 255, 255), tx + S(14), ty + S(16), tw2 - S(20), S(36), v, DT_SINGLELINE);
            text(m, f_lbl, C_LBL, tx + S(14), ty + S(52), tw2 - S(20), S(18), lbl[i], DT_SINGLELINE);
        }
        EnterCriticalSection(&g_cs);
        int my = ty + th2 + S(16);
        text(m, f_small, C_DIM, s.left + S(22), my, sw - S(44), sh - (my - s.top) - S(14), g_status, DT_WORDBREAK);
        LeaveCriticalSection(&g_cs);
    }
}

static void paint(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    HDC m = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, L.W, L.H);
    HGDIOBJ old = SelectObject(m, bm);
    SetBkMode(m, TRANSPARENT);
    void *G; GdipCreateFromHDC(m, &G);
    GdipSetSmoothingMode(G, 4); GdipSetPixelOffsetMode(G, 4); GdipSetInterpolationMode(G, 7);
    /* backdrop: blurred stadium + dark gradient */
    void *bg; GdipCreateSolidFill(ARGB(255, 22, 32, 44), &bg); GdipFillRectangleI(G, bg, 0, 0, L.W, L.H); GdipDeleteBrush(bg);
    EnterCriticalSection(&g_imgcs); draw_img(G, g_backdrop, 0, 0, L.W, L.H); LeaveCriticalSection(&g_imgcs);
    vgrad(G, 0, 0, L.W, L.H, ARGB(70, 14, 22, 32), ARGB(180, 16, 24, 34));

    /* hero box */
    RECT hr = L.hero; int hw = hr.right - hr.left, hh = hr.bottom - hr.top, rad = S(16);
    for (int i = 6; i >= 1; i--) rr_fill(G, hr.left - S(i), hr.top - S(i) + S(6), hw + S(2 * i), hh + S(2 * i), rad + S(i), ARGB(16, 0, 0, 0));
    void *clip = rr_path(hr.left, hr.top, hw, hh, rad); GdipSetClipPath(G, clip, 0);
    EnterCriticalSection(&g_imgcs); draw_img(G, g_banner, hr.left, hr.top, hw, hh); LeaveCriticalSection(&g_imgcs);
    vgrad(G, hr.left, hr.bottom - S(60), hw, S(60), ARGB(0, 0, 0, 0), ARGB(90, 0, 0, 0));
    GdipResetClip(G); GdipDeletePath(clip);
    rr_line(G, hr.left, hr.top, hw, hh, rad, ARGB(70, 255, 255, 255));

    draw_title(m, G);
    draw_window_buttons(G);
    draw_action(m, G);
    draw_cards(m, G);

    GdipDeleteGraphics(G);
    BitBlt(dc, 0, 0, L.W, L.H, m, 0, 0, SRCCOPY);
    SelectObject(m, old); DeleteObject(bm); DeleteDC(m);
    EndPaint(h, &ps);
}
/* ------------------------------------------------------------ window */
static int hit_item(int x, int y) {
    if (in_rect(&L.bclose, x, y)) return HOT_CLOSE;
    if (in_rect(&L.bmin, x, y)) return HOT_MIN;
    if (in_rect(&L.play, x, y)) return HOT_PLAY;
    return HOT_NONE;
}
static void set_hot(HWND h, int hot) {
    if (hot == g_hot) return;
    g_hot = hot; InvalidateRect(h, NULL, FALSE);
    TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
}
static int g_paint_pending;
static void request_paint(HWND h) {
    if (!g_paint_pending) { g_paint_pending = 1; SetTimer(h, 1, 33, NULL); }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        calc_layout(h);
        MARGINS mg = { 0, 0, 0, 1 }; DwmExtendFrameIntoClientArea(h, &mg);           /* keeps the window shadow */
        DWORD round = 2; DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof round);
        BOOL dark = TRUE; DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
        SetTimer(h, 2, 1000, NULL);
        SetTimer(h, 3, 30, NULL);                                                     /* update-arrow animation */
        {   /* crisp title-bar / taskbar / Alt-Tab icons from the embedded logo icon */
            HINSTANCE hi = GetModuleHandleW(NULL);
            HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
            HICON sm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
            if (big) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big);
            if (sm) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)sm);
        }
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        return 0;
    }
    case WM_NCCALCSIZE:
        if (w) return 0;                                                         /* whole window is client area */
        break;
    case WM_NCHITTEST: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p);
        int b = S(6);
        int left = p.x < b, right = p.x >= L.W - b, top = p.y < b, bottom = p.y >= L.H - b;
        if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
        if (p.y < L.tb && hit_item(p.x, p.y) == HOT_NONE) return HTCAPTION;
        if (in_rect(&L.hero, p.x, p.y)) return HTCAPTION;               /* drag by the banner too */
        return HTCLIENT;
    }
    case WM_SIZE: calc_layout(h); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = S(900); ((MINMAXINFO *)l)->ptMinTrackSize.y = S(560); return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w); make_fonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        calc_layout(h); InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(h); return 0;
    case WM_MOUSEMOVE: set_hot(h, hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l))); return 0;
    case WM_MOUSELEAVE: g_hot = HOT_NONE; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && g_hot == HOT_PLAY && btn_enabled()) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: g_pressed = hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l)); if (g_pressed) SetCapture(h); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONUP: {
        int it = hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l)), pr = g_pressed;
        g_pressed = HOT_NONE; ReleaseCapture(); InvalidateRect(h, NULL, FALSE);
        if (it != pr) return 0;
        if (it == HOT_CLOSE) PostMessage(h, WM_CLOSE, 0, 0);
        else if (it == HOT_MIN) ShowWindow(h, SW_MINIMIZE);
        else if (it == HOT_PLAY && btn_enabled()) { if (g_state == ST_READY) play(); else g_go = 1; }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p);
        if (in_rect(&L.notes, p.x, p.y)) { g_scroll -= GET_WHEEL_DELTA_WPARAM(w) * S(60) / WHEEL_DELTA; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_KEYDOWN:
        if ((w == VK_RETURN || w == VK_SPACE) && btn_enabled()) { if (g_state == ST_READY) play(); else g_go = 1; }
        return 0;
    case WM_TIMER:
        if (w == 1) { KillTimer(h, 1); g_paint_pending = 0; InvalidateRect(h, NULL, FALSE); }
        else if (w == 3) {
            if (spinning()) { g_spin += 9.0f; if (g_spin >= 360) g_spin -= 360; RECT pr = L.play; InflateRect(&pr, S(12), S(12)); InvalidateRect(h, &pr, FALSE); }
        }
        else if (w == 2) {      /* download speed, smoothed */
            LONGLONG d = g_done - g_last_done; g_last_done = g_done;
            if (g_state == ST_DOWNLOADING && d >= 0) g_speed = g_speed ? g_speed * 0.7 + d * 0.3 : (double)d;
            else g_speed = 0;
        }
        return 0;
    case WM_REFRESH:
        if (w == 1) g_scroll = 0;
        request_paint(h);
        return 0;
    case WM_APP + 0x77: ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

/* banner from the server (optional, the owner can change it) */
static void fetch_banner(void) {
    HINTERNET r = open_req(L"/launcher/banner", 0);
    if (!r) return;
    if (status_code(r) != 200) { WinHttpCloseHandle(r); return; }
    size_t cap = 1 << 20, n = 0; char *buf = malloc(cap);
    for (;;) {
        DWORD got = 0;
        if (n + 65536 > cap) { if (cap > (16u << 20)) break; cap *= 2; buf = realloc(buf, cap); }
        if (!WinHttpReadData(r, buf + n, 65536, &got) || !got) break;
        n += got;
    }
    WinHttpCloseHandle(r);
    void *img = n ? load_image_mem(buf, (UINT)n) : NULL;
    if (img) {
        wchar_t p[MAX_PATH * 2]; swprintf(p, MAX_PATH * 2, L"%ls\\banner.img", g_base);
        FILE *f = _wfopen(p, L"wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); }
        EnterCriticalSection(&g_imgcs); void *o = g_banner; g_banner = img; LeaveCriticalSection(&g_imgcs);
        if (o) GdipDisposeImage(o);
        PostMessage(g_wnd, WM_REFRESH, 0, 0);
    }
    free(buf);
}

static void steam_refresh(void) {
    SteamInfo keep = g_steam; memset(&g_steam, 0, sizeof g_steam);
    read_steam_info();
    if (!g_steam.ok) g_steam = keep;
    PostMessage(g_wnd, WM_REFRESH, 0, 0);
}

static DWORD WINAPI side_worker(LPVOID u) {
    (void)u;
    read_steam_info();
    if (g_steam.ok) {
        void *a = load_image_file(g_steam.avatar_path);
        EnterCriticalSection(&g_imgcs); g_avatar = a; LeaveCriticalSection(&g_imgcs);
    }
    PostMessage(g_wnd, WM_REFRESH, 0, 0);
    fetch_banner();
    return 0;
}

/* ------------------------------------------------------------ main */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev; (void)cmd;
    /* crisp on scaled displays (125% / 150% ...) */
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *ctx_fn)(HANDLE);
    ctx_fn setctx = (ctx_fn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (!setctx || !setctx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */)) SetProcessDPIAware();
    InitializeCriticalSection(&g_cs); InitializeCriticalSection(&g_imgcs);
    { wchar_t me[MAX_PATH], old[MAX_PATH + 8]; if (GetModuleFileNameW(NULL, me, MAX_PATH)) { swprintf(old, MAX_PATH + 8, L"%ls.old", me); DeleteFileW(old); } }   /* left by an update */
    (void)g_build_tag;
    { ULONG_PTR tok; GpStartupInput gi = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&tok, &gi, NULL); }
    /* folders */
    wchar_t la[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) wcscpy(la, L"C:\\");
    swprintf(g_base, MAX_PATH, L"%ls\\UE4PSO", la); CreateDirectoryW(g_base, NULL);
    swprintf(g_game, MAX_PATH, L"%ls\\Game", g_base); CreateDirectoryW(g_game, NULL);
    { wchar_t p[MAX_PATH * 2]; swprintf(p, MAX_PATH * 2, L"%ls\\installed.txt", g_base);
      g_installed_once = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
    { wchar_t ini[MAX_PATH]; GetModuleFileNameW(NULL, ini, MAX_PATH);
      wchar_t *s = wcsrchr(ini, L'\\'); if (s) wcscpy(s + 1, L"PSOLauncher.ini");
      FILE *f = _wfopen(ini, L"r"); char line[200];
      if (f) { while (fgets(line, sizeof line, f)) if (!strncmp(line, "server=", 7)) {
                   line[strcspn(line, "\r\n ")] = 0; MultiByteToWideChar(CP_UTF8, 0, line + 7, -1, g_server, 128); }
               fclose(f); } }
    load_cache();
    { wchar_t bp[MAX_PATH * 2]; swprintf(bp, MAX_PATH * 2, L"%ls\\banner.img", g_base);
      g_banner = load_image_file(bp);
      if (!g_banner) g_banner = load_image_mem(HERO_JPG, sizeof HERO_JPG);
      g_backdrop = load_image_mem(BACKDROP_JPG, sizeof BACKDROP_JPG);
      g_logo = load_image_mem(LOGO_PNG, sizeof LOGO_PNG); }
    g_ses = WinHttpOpen(L"UE4PSOLauncher/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!g_ses) g_ses = WinHttpOpen(L"UE4PSOLauncher/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    WinHttpSetTimeouts(g_ses, 10000, 10000, 15000, 30000);

    HDC sdc = GetDC(NULL); g_dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(NULL, sdc);
    make_fonts();
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"UE4PSOLauncher";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(hi, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);
    int ww = S(1040), wh = S(640);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (wh > wa.bottom - wa.top) wh = wa.bottom - wa.top; if (ww > wa.right - wa.left) ww = wa.right - wa.left;
    g_wnd = CreateWindowExW(WS_EX_APPWINDOW, L"UE4PSOLauncher", L"PSO UE4 Launcher",
                            WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                            wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, NULL, NULL, hi, NULL);
    /* per-monitor DPI of the monitor the window opened on */
    typedef UINT (WINAPI *dpi_fn)(HWND);
    dpi_fn getdpi = (dpi_fn)GetProcAddress(u32, "GetDpiForWindow");
    if (getdpi) { UINT d = getdpi(g_wnd); if (d && (int)d != g_dpi) { g_dpi = d; make_fonts(); calc_layout(g_wnd); } }
    ShowWindow(g_wnd, show); UpdateWindow(g_wnd);
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, side_worker, NULL, 0, NULL));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

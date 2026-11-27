/* PSOLauncher.exe - "PSO UE4 Launcher"
 * Installs / updates the private-server version of Pro Soccer Online from the PSO server and starts it.
 * Game goes to %LOCALAPPDATA%\UE4PSO\Game (no admin rights needed, Steam's copy is untouched).
 * Server address: DEFAULT_SERVER, or "server=..." in PSOLauncher.ini next to the exe.
 * Build 26 (secure downloads): the server's certificate is checked like a browser does (no more "accept any certificate"),
 * the file list must carry the owner's signature (ECDSA P-256, public key below) before anything is downloaded, every file is
 * checked with SHA-256, and a launcher update is only taken when its SHA-256 is in that signed list. */
#define PSO_BUILD 26          /* launcher build number (the self-update compares it) */
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
#include <shlobj.h>
#include <shobjidl.h>

#define DEFAULT_SERVER L"play.pso-ue4.com"       /* a name, so the server can move without a new launcher (and the certificate is for it) */
/* the owner's file-list key (public half; the private half never leaves the owner's PC): X || Y of an ECDSA P-256 key */
static const unsigned char FILES_PUB[64] = { 0x05, 0xa8, 0x2c, 0x0a, 0xe6, 0x9e, 0x0a, 0x1e, 0xf6, 0x91, 0x23, 0x4c, 0xcd, 0x30, 0x69, 0x45, 0x43, 0x27, 0xcd, 0x28, 0x44, 0xd4, 0x05, 0xba, 0x8a, 0x34, 0xe9, 0x25, 0xf1, 0x72, 0xe9, 0x17, 0x4e, 0x27, 0xc1, 0x88, 0xd9, 0x5e, 0x48, 0xf0, 0x07, 0x53, 0x1b, 0x11, 0xce, 0xe7, 0x23, 0xae, 0x2b, 0xc5, 0xc7, 0x58, 0x33, 0xfc, 0x68, 0xce, 0xe2, 0x34, 0x0a, 0xd2, 0xd6, 0x52, 0xee, 0x3f };
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
/* the player's own choices, kept in %LOCALAPPDATA%\UE4PSO\launcher-settings.txt */
static int g_stay_open;               /* 1 = the launcher stays on screen while the game runs (0 = it hides and comes back afterwards) */
static int g_anim_pref = 1;           /* 1 = opening, closing and the moving background */
static int g_sys_motion = 1;          /* Windows' own "show animations" switch */
static volatile int g_updating;       /* a newer launcher is being put in place: nothing else may start */
static volatile int g_ui_busy;        /* a Windows dialog of ours is open (folder choice) */

static void set_status(int st, const wchar_t *fmt, ...) {
    EnterCriticalSection(&g_cs);
    if (st >= 0) g_state = st;
    if (fmt) { va_list ap; va_start(ap, fmt); _vsnwprintf(g_status, 255, fmt, ap); va_end(ap); g_status[255] = 0; }
    LeaveCriticalSection(&g_cs);
    PostMessage(g_wnd, WM_REFRESH, 3, 0);      /* 3 = state / status line */
}

/* ------------------------------------------------------------ HTTP (HTTPS, certificate checked by Windows) */
static HINTERNET g_ses; static int g_from_ini;
static volatile DWORD g_net_err;                 /* last connection error (ERROR_WINHTTP_SECURE_FAILURE etc.) */
static int tls_error(DWORD e) { return e == 12175 /* SECURE_FAILURE */ || e == 12037 || e == 12038 || e == 12044 || e == 12045 || e == 12169 || e == 12179 || e == 12157; }
static HINTERNET open_req_r(const wchar_t *path, LONGLONG range_from, LONGLONG range_to);
static HINTERNET open_req(const wchar_t *path, LONGLONG range_from) { return open_req_r(path, range_from, -1); }
static HINTERNET open_req_r(const wchar_t *path, LONGLONG range_from, LONGLONG range_to) {   /* range_to: last byte wanted, -1 = to the end */
    HINTERNET c = WinHttpConnect(g_ses, g_server, 443, 0);
    if (!c) { g_net_err = GetLastError(); return NULL; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!r) { g_net_err = GetLastError(); WinHttpCloseHandle(c); return NULL; }
    wchar_t hdr[64] = L"";
    if (range_to >= 0) swprintf(hdr, 64, L"Range: bytes=%lld-%lld", range_from, range_to);
    else if (range_from > 0) swprintf(hdr, 64, L"Range: bytes=%lld-", range_from);
    if (!WinHttpSendRequest(r, hdr[0] ? hdr : WINHTTP_NO_ADDITIONAL_HEADERS, hdr[0] ? (DWORD)-1L : 0, NULL, 0, 0, 0)
        || !WinHttpReceiveResponse(r, NULL)) { g_net_err = GetLastError(); WinHttpCloseHandle(r); WinHttpCloseHandle(c); return NULL; }
    g_net_err = 0;
    return r;   /* connection handle is closed together with the session; fine for this program */
}
static HINTERNET open_req_host(const wchar_t *host, const wchar_t *path, LONGLONG range_from, LONGLONG range_to) {
    HINTERNET c = WinHttpConnect(g_ses, host, 443, 0); if (!c) return NULL;
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); return NULL; }
    wchar_t hdr[64]; swprintf(hdr, 64, L"Range: bytes=%lld-%lld", range_from, range_to);
    if (!WinHttpSendRequest(r, hdr, (DWORD)-1L, NULL, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) { WinHttpCloseHandle(r); WinHttpCloseHandle(c); return NULL; }
    return r;
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

/* POST a JSON body; the answer like http_get (certificate checked, like every request) */
static char *http_post_one(const wchar_t *host, const wchar_t *path, const char *json, DWORD *code);
static char *http_post_json(const wchar_t *path, const char *json, DWORD *code) { return http_post_one(g_server, path, json, code); }
static char *http_post_one(const wchar_t *host, const wchar_t *path, const char *json, DWORD *code) {
    *code = 0;
    HINTERNET c = WinHttpConnect(g_ses, host, 443, 0);
    if (!c) return NULL;
    HINTERNET r = WinHttpOpenRequest(c, L"POST", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); return NULL; }
    DWORD n = (DWORD)strlen(json);
    if (!WinHttpSendRequest(r, L"Content-Type: application/json", (DWORD)-1L, (void *)json, n, n, 0) || !WinHttpReceiveResponse(r, NULL)) {
        WinHttpCloseHandle(r); WinHttpCloseHandle(c); return NULL; }
    *code = status_code(r);
    size_t cap = 1 << 14, len = 0; char *buf = malloc(cap);
    for (;;) { DWORD got = 0;
        if (len + 8192 + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
        if (!WinHttpReadData(r, buf + len, 8192, &got) || !got) break;
        len += got; }
    buf[len] = 0;
    WinHttpCloseHandle(r); WinHttpCloseHandle(c);
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
typedef struct { char p[400]; LONGLONG s; char h[41], h2[65]; int need, cdn; } FileEnt;   /* h: SHA-1 (only names the file on Cloudflare), h2: SHA-256 (what is checked); cdn: also on the server's Cloudflare storage */
static wchar_t g_cdn[128];              /* e.g. files.pso-ue4.com (from the manifest), empty = none */
static volatile int g_cdn_bad;          /* Cloudflare failed in this run: only the server from then on (small files) */
static volatile LONG g_cdn_nofail;      /* Cloudflare connections that failed in a row */
static FileEnt *g_files; static int g_nfiles, g_version;
static void llog(const char *fmt, ...);
static volatile int g_signed;                    /* the file list now in g_files carries the owner's valid signature */
static CRITICAL_SECTION g_selfcs; static LONGLONG g_self_s; static char g_self_h2[65];   /* the launcher the signed list names (0 = none) */

typedef struct { char p[400]; LONGLONG s; LONGLONG mt; char h[65]; } CacheEnt;   /* h = SHA-256 */
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
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\files2.cache", g_base);   /* build 26: SHA-256 (files.cache had SHA-1) */
    FILE *f = _wfopen(path, L"r"); if (!f) return;
    char line[600];
    g_cache = calloc(4096, sizeof(CacheEnt));
    while (g_ncache < 4096 && fgets(line, sizeof line, f)) {
        CacheEnt *c = &g_cache[g_ncache];
        if (sscanf(line, "%64[0-9a-f]|%lld|%lld|%399[^\r\n]", c->h, &c->s, &c->mt, c->p) == 4 && strlen(c->h) == 64) g_ncache++;
    }
    fclose(f);
}
static void save_cache(void) {
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\files2.cache", g_base);   /* build 26: SHA-256 (files.cache had SHA-1) */
    FILE *f = _wfopen(path, L"w"); if (!f) return;
    for (int i = 0; i < g_nfiles; i++) {
        wchar_t fp[MAX_PATH * 2]; LONGLONG s, mt;
        path_of(g_files[i].p, fp);
        if (!g_files[i].need && file_info(fp, &s, &mt)) fprintf(f, "%s|%lld|%lld|%s\n", g_files[i].h2, s, mt, g_files[i].p);
    }
    fclose(f);
}

/* SHA-256 of a file (with progress for big files) */
static int sha256_file(const wchar_t *path, char out[65], int show) {
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h; unsigned char dig[32];
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) { CloseHandle(f); return 0; }
    BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    unsigned char *buf = (unsigned char *)malloc(1 << 20); DWORD got; LONGLONG tot = 0, size, mt;   /* (own buffer: files are checked by several threads) */
    if (!buf) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(f); return 0; }
    file_info(path, &size, &mt);
    while (ReadFile(f, buf, 1 << 20, &got, NULL) && got) {
        BCryptHashData(h, buf, got, 0);
        tot += got;
        if (show && size > (64 << 20) && (tot & ((64 << 20) - 1)) < got)
            set_status(-1, L"Checking game files... %d%%", (int)(tot * 100 / size));
    }
    free(buf);
    BCryptFinishHash(h, dig, 32, 0);
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(f);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", dig[i]);
    return 1;
}
static int sha256_buf(const void *data, size_t n, unsigned char dig[32]) {
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h; int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) return 0;
    if (!BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0)) {
        ok = !BCryptHashData(h, (PUCHAR)data, (ULONG)n, 0) && !BCryptFinishHash(h, dig, 32, 0);
        BCryptDestroyHash(h); }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
static void hex_of(const unsigned char *b, int n, char *out) { for (int i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]); }
/* the owner's signature (r || s, 64 bytes as 128 hex characters) over SHA-256(msg) */
static int sig_ok(const char *msg, size_t n, const char *sighex) {
    unsigned char sig[64], dig[32]; int ok = 0;
    if (!sighex || strlen(sighex) != 128) return 0;
    for (int i = 0; i < 64; i++) { unsigned v; if (sscanf(sighex + i * 2, "%2x", &v) != 1) return 0; sig[i] = (unsigned char)v; }
    if (!sha256_buf(msg, n, dig)) return 0;
    BCRYPT_ALG_HANDLE a; BCRYPT_KEY_HANDLE k;
    if (BCryptOpenAlgorithmProvider(&a, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0)) return 0;
    struct { BCRYPT_ECCKEY_BLOB h; unsigned char xy[64]; } blob;
    blob.h.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC; blob.h.cbKey = 32; memcpy(blob.xy, FILES_PUB, 64);
    if (!BCryptImportKeyPair(a, NULL, BCRYPT_ECCPUBLIC_BLOB, &k, (PUCHAR)&blob, sizeof blob, 0)) {
        ok = BCryptVerifySignature(k, NULL, dig, 32, sig, 64, 0) == 0;
        BCryptDestroyKey(k); }
    BCryptCloseAlgorithmProvider(a, 0);
    return ok;
}
static int is_hex(const char *s, int n) { for (int i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) return 0; return 1; }
static int fe_cmp(const void *a, const void *b) { return strcmp(((const FileEnt *)a)->p, ((const FileEnt *)b)->p); }
/* What the owner signs (the backend's /launcher/canonical shows the same text):
     "PSO-FILES-1\n", then for every file sorted by path "path<TAB>size<TAB>sha256\n", then "@launcher<TAB>size<TAB>sha256\n"
   when the server hands out a launcher. Version number and patch notes are not part of it (they change nothing on disk). */
static int verify_list(const char *sighex, LONGLONG self_s, const char *self_h2) {
    FileEnt *srt = malloc((size_t)g_nfiles * sizeof(FileEnt)); if (!srt) return 0;
    memcpy(srt, g_files, (size_t)g_nfiles * sizeof(FileEnt)); qsort(srt, g_nfiles, sizeof(FileEnt), fe_cmp);
    size_t cap = (size_t)g_nfiles * 500 + 256, n = 0; char *t = malloc(cap); int ok = 0;
    if (t) {
        n += sprintf(t + n, "PSO-FILES-1\n");
        for (int i = 0; i < g_nfiles; i++) n += sprintf(t + n, "%s\t%lld\t%s\n", srt[i].p, srt[i].s, srt[i].h2);
        if (self_s > 0) n += sprintf(t + n, "@launcher\t%lld\t%s\n", self_s, self_h2);
        ok = sig_ok(t, n, sighex);
        free(t); }
    free(srt);
    return ok;
}

static int parse_manifest(const char *js) {
    double v;
    if (!json_num(js, "version", &v)) return 0;
    g_version = (int)v;
    g_signed = 0;
    g_cdn[0] = 0;
    { const char *c = strstr(js, "\"cdn\": \""); if (c) { c += 8; int k = 0; while (*c && *c != '"' && k < 120 && (isalnum((unsigned char)*c) || *c == '.' || *c == '-')) g_cdn[k++] = (wchar_t)*c++; g_cdn[k] = 0; if (*c != '"') g_cdn[0] = 0; } }
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
        const char *nx = strstr(p, "{\"p\": \""), *h2 = strstr(p, "\"h2\": \"");
        if (!h2 || (nx && h2 > nx) || !is_hex(h2 + 7, 64)) return 0;   /* build 26: SHA-256 required */
        memcpy(e->h2, h2 + 7, 64); e->h2[64] = 0;
        { const char *cm = strstr(p, "\"c\": 1"); e->cdn = cm && (!nx || cm < nx); }
        g_nfiles++;
    }
    if (g_nfiles <= 0) return 0;
    /* the owner's signature over the list (and the launcher it names) */
    LONGLONG ss = 0; char sh[65] = "";
    const char *sf = strstr(js, "\"self\": {");
    if (sf) { double d; const char *q = strstr(sf, "\"h2\": \"");
        if (json_num(sf, "s", &d) && q && is_hex(q + 7, 64)) { ss = (LONGLONG)d; memcpy(sh, q + 7, 64); sh[64] = 0; } }
    char sig[129] = ""; const char *sg = strstr(js, "\"sig\": \"");
    if (sg && is_hex(sg + 8, 128) && sg[136] == '"') { memcpy(sig, sg + 8, 128); sig[128] = 0; }
    g_signed = sig[0] && verify_list(sig, ss, sh);
    EnterCriticalSection(&g_selfcs);
    if (g_signed) { g_self_s = ss; strcpy(g_self_h2, sh); }
    LeaveCriticalSection(&g_selfcs);
    llog("file list version %d: %d files, signature %s", g_version, g_nfiles, g_signed ? "valid" : sig[0] ? "INVALID" : "missing");
    return 1;
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
            char h[65];
            if (known) e->need = strcmp(known, e->h2) != 0;
            else if (sha256_file(fp, h, 1)) e->need = strcmp(h, e->h2) != 0;
        }
        if (e->need) need += e->s;
    }
    return need;
}

/* ---- big files: several pieces at once (one connection over a long distance, e.g. Europe <-> the server, is capped far
 * below the player's line; 6 of them together are not). Each piece resumes on its own; where each one is, is kept in
 * "<file>.part.prog" (written every second), so closing the launcher loses nothing. */
#define PAR_N 6
#define PAR_MIN (64LL << 20)                       /* files from 64 MB */
typedef struct { const wchar_t *url, *part, *curl; LONGLONG start, end; volatile LONGLONG done; volatile int ok, fail, cdn_fails; } ParSeg;   /* curl: the same file on Cloudflare (NULL = only the server) */
/* launcher.log next to the game (%LOCALAPPDATA%\UE4PSO\launcher.log): only download problems, at most ~200 lines a run */
static CRITICAL_SECTION g_lcs; static int g_lcs_ok; static volatile LONG g_lines;
static void llog(const char *fmt, ...) {
    if (InterlockedIncrement(&g_lines) > 200) return;
    if (!g_lcs_ok) { InitializeCriticalSection(&g_lcs); g_lcs_ok = 1; }
    EnterCriticalSection(&g_lcs);
    wchar_t path[MAX_PATH * 2]; swprintf(path, MAX_PATH * 2, L"%ls\\launcher.log", g_base);
    FILE *f = _wfopen(path, g_lines == 1 ? L"w" : L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lcs);
}
static DWORD WINAPI par_thread(LPVOID u) {
    ParSeg *g = (ParSeg *)u; char *buf = (char *)malloc(1 << 18); if (!buf) return 0;
    /* Cloudflare first; a piece only moves to the server after Cloudflare failed it 8 times in a row (a hiccup in the
       middle of 4.5 GB must not slow the whole rest down). Both send exactly the same bytes, so it carries on where it is. */
    for (int tries = 0; tries < 20 && g->start + g->done < g->end; tries++) {
        int cdn = g->curl && g_cdn[0] && g->cdn_fails < 8;
        if (tries) Sleep(cdn ? 1000 + 500 * (g->cdn_fails < 6 ? g->cdn_fails : 6) : 1500);
        LONGLONG at = g->start + g->done;
        HINTERNET r = cdn ? open_req_host(g_cdn, g->curl, at, g->end - 1) : open_req_r(g->url, at, g->end - 1);
        if (!r) { if (cdn) g->cdn_fails++; llog("piece %lld-%lld: %s didn't answer (error %lu), try %d", g->start, g->end, cdn ? "Cloudflare" : "server", GetLastError(), tries + 1); continue; }
        if (status_code(r) != 206) {
            llog("piece %lld-%lld: %s answered %lu", g->start, g->end, cdn ? "Cloudflare" : "server", status_code(r));
            WinHttpCloseHandle(r);
            if (cdn) { g->cdn_fails = 8; continue; }                 /* Cloudflare doesn't have it: the server */
            g->fail = 1; free(buf); return 0;                      /* the server doesn't send pieces: the caller goes back to one connection */
        }
        HANDLE f = CreateFileW(g->part, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(r); continue; }
        LARGE_INTEGER z; z.QuadPart = at; SetFilePointerEx(f, z, NULL, FILE_BEGIN);
        DWORD got, wr; LONGLONG before = g->done;
        for (;;) {
            LONGLONG left = g->end - (g->start + g->done); if (left <= 0) break;
            if (!WinHttpReadData(r, buf, (DWORD)(left < (1 << 18) ? left : (1 << 18)), &got) || !got) break;
            if (!WriteFile(f, buf, got, &wr, NULL) || wr != got) break;
            g->done += got; InterlockedExchangeAdd64(&g_done, got);
            PostMessage(g_wnd, WM_REFRESH, 2, 0);
        }
        DWORD le = GetLastError();
        CloseHandle(f); WinHttpCloseHandle(r);
        if (g->start + g->done < g->end) llog("piece %lld-%lld: %s stopped after %lld MB this time (error %lu), at %lld of %lld MB", g->start, g->end, cdn ? "Cloudflare" : "server", (g->done - before) >> 20, le, g->done >> 20, (g->end - g->start) >> 20);
        if (cdn) { if (g->done > before) g->cdn_fails = 0; else g->cdn_fails++; if (g->cdn_fails == 8) llog("piece %lld-%lld: moves to the server", g->start, g->end); }   /* it was moving: a hiccup, not a broken Cloudflare */
        if (g->done > before) tries = 0;                           /* progress: keep going as long as it moves */
    }
    g->ok = g->start + g->done >= g->end;
    free(buf);
    return 0;
}
static void par_save(const wchar_t *prog, LONGLONG size, ParSeg *sg, int n) {
    FILE *f = _wfopen(prog, L"w"); if (!f) return;
    fprintf(f, "%lld %d\n", size, n);
    for (int i = 0; i < n; i++) fprintf(f, "%lld %lld %lld\n", sg[i].start, sg[i].end, (LONGLONG)sg[i].done);
    fclose(f);
}
/* 1 = done (the .part is complete), 0 = not finished (try again later), -1 = the server doesn't send pieces */
static int download_parallel(FileEnt *e, const wchar_t *url, const wchar_t *part, const wchar_t *curl) {
    wchar_t prog[MAX_PATH * 2 + 8]; swprintf(prog, MAX_PATH * 2 + 8, L"%ls.prog", part);
    ParSeg sg[PAR_N]; memset(sg, 0, sizeof sg); int n = 0;
    LONGLONG have = 0, mt; int have_part = file_info(part, &have, &mt);
    FILE *pf = _wfopen(prog, L"r");                          /* pieces from last time? */
    if (pf) { LONGLONG sz = 0; int k = 0;
        if (fscanf(pf, "%lld %d", &sz, &k) == 2 && sz == e->s && k > 0 && k <= PAR_N && have_part && have == e->s) {
            for (n = 0; n < k; n++) { LONGLONG a, b, d; if (fscanf(pf, "%lld %lld %lld", &a, &b, &d) != 3 || a < 0 || b > sz || a > b || d < 0 || d > b - a) break; sg[n].start = a; sg[n].end = b; sg[n].done = d; }
            if (n != k) n = 0; }
        fclose(pf); }
    if (!n && have_part && have == e->s && !pf) return 1;    /* a complete .part from before: the hash check decides */
    if (!n) {                                                /* new (or an old one-connection .part: keep what it has) */
        LONGLONG from = (have_part && have < e->s) ? have : 0, rest = e->s - from;
        HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, from ? OPEN_ALWAYS : CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) return 0;
        LARGE_INTEGER z; z.QuadPart = e->s; SetFilePointerEx(f, z, NULL, FILE_BEGIN); SetEndOfFile(f); CloseHandle(f);   /* the whole size now: the pieces write into it */
        n = PAR_N; LONGLONG per = (rest + n - 1) / n;
        for (int i = 0; i < n; i++) { sg[i].start = from + per * i; sg[i].end = from + per * (i + 1); if (sg[i].end > e->s) sg[i].end = e->s; if (sg[i].start > e->s) sg[i].start = e->s; }
        g_done += from;
    }
    for (int i = 0; i < n; i++) { sg[i].url = url; sg[i].part = part; sg[i].curl = curl; g_done += sg[i].done; }
    par_save(prog, e->s, sg, n);
    HANDLE th[PAR_N]; int nt = 0;
    for (int i = 0; i < n; i++) if (sg[i].done < sg[i].end - sg[i].start) { th[nt] = CreateThread(NULL, 0, par_thread, &sg[i], 0, NULL); if (th[nt]) nt++; } else sg[i].ok = 1;
    while (nt && WaitForMultipleObjects(nt, th, TRUE, 1000) == WAIT_TIMEOUT) par_save(prog, e->s, sg, n);
    for (int i = 0; i < nt; i++) CloseHandle(th[i]);
    par_save(prog, e->s, sg, n);
    int all = 1, nopieces = 0; for (int i = 0; i < n; i++) { if (!sg[i].ok) all = 0; if (sg[i].fail) nopieces = 1; }
    if (nopieces) { DeleteFileW(prog); DeleteFileW(part); return -1; }
    if (!all) return 0;
    DeleteFileW(prog);
    return 1;
}

static __thread LONGLONG t_gd;                 /* what this thread added to g_done (undone when its try fails) */
static void gd_add(LONGLONG v) { InterlockedExchangeAdd64(&g_done, v); t_gd += v; }
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

    LONGLONG have = 0, mt; int from_cdn = 0;
    static int no_pieces;                                    /* the server answered whole files only: one connection from then on */
    if (e->s >= PAR_MIN && !no_pieces) {                        /* big: 6 pieces, each from Cloudflare first (near the player), the server as backup */
        wchar_t cu[64]; swprintf(cu, 64, L"/%hs", e->h);
        int use_cdn = e->cdn && g_cdn[0];
        int pr = download_parallel(e, url, part, use_cdn ? cu : NULL);
        if (pr == 1 && use_cdn) from_cdn = 1;
        if (pr == 0) return 0;
        if (pr < 0) no_pieces = 1;
        if (pr == 1) goto check;
    }
    if (!file_info(part, &have, &mt)) have = 0;
    if (have > e->s) { DeleteFileW(part); have = 0; }
    gd_add(have);
    if (have < e->s) {
        HINTERNET r = NULL;
        if (e->cdn && g_cdn[0] && !g_cdn_bad && e->s > 0) {         /* Cloudflare first: a server near the player answers at once */
            wchar_t cu[64]; swprintf(cu, 64, L"/%hs", e->h);
            r = open_req_host(g_cdn, cu, have, e->s - 1);
            if (r && status_code(r) == 206) { from_cdn = 1; g_cdn_nofail = 0; }
            else { llog("%s: Cloudflare %s %lu", e->p, r ? "answered" : "didn't answer, error", r ? status_code(r) : GetLastError()); if (r) WinHttpCloseHandle(r); else if (InterlockedIncrement(&g_cdn_nofail) >= 5) { g_cdn_bad = 1; llog("Cloudflare off for small files in this run"); } r = NULL; }   /* (5 times no answer in a row: the server for the rest of this run) */
        }
        if (!r) r = open_req(url, have);
        if (!r) return 0;
        DWORD code = status_code(r);
        if (code == 200 && have) { gd_add(-have); have = 0; }          /* server ignored the resume */
        else if (code != 206 && code != 200) { WinHttpCloseHandle(r); return 0; }
        HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, have ? OPEN_ALWAYS : CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(r); return 0; }
        LARGE_INTEGER z; z.QuadPart = have; SetFilePointerEx(f, z, NULL, FILE_BEGIN); SetEndOfFile(f);
        char *buf = (char *)malloc(1 << 18); DWORD got, wr; int ok = 1;     /* (own buffer: 8 files at once) */
        if (!buf) { CloseHandle(f); WinHttpCloseHandle(r); return 0; }
        for (;;) {
            if (!WinHttpReadData(r, buf, 1 << 18, &got)) { ok = 0; break; }
            if (!got) break;
            if (!WriteFile(f, buf, got, &wr, NULL) || wr != got) { ok = 0; break; }
            have += got; gd_add(got);
            PostMessage(g_wnd, WM_REFRESH, 2, 0);      /* 2 = download progress only */
        }
        free(buf);
        CloseHandle(f); WinHttpCloseHandle(r);
        if (!ok || have != e->s) return 0;
    }
check:;
    char h[65];
    if (!g_signed || !sha256_file(part, h, 0) || strcmp(h, e->h2)) { DeleteFileW(part); if (from_cdn) e->cdn = 0; return 0; }
    SetFileAttributesW(fp, FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(part, fp, MOVEFILE_REPLACE_EXISTING)) return 0;
    e->need = 0;
    return 1;
}

static volatile LONG g_sm_next, g_sm_fail;
static DWORD WINAPI small_thread(LPVOID u) {           /* takes the next small file that's needed, until none are left */
    (void)u;
    for (;;) {
        int i = -1;
        for (;;) { LONG k = InterlockedIncrement(&g_sm_next) - 1; if (k >= g_nfiles) break; if (g_files[k].need && g_files[k].s < PAR_MIN) { i = (int)k; break; } }
        if (i < 0 || g_sm_fail) return 0;
        int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {
            t_gd = 0;
            ok = download_file(&g_files[i]);
            if (!ok) { InterlockedExchangeAdd64(&g_done, -t_gd); Sleep(1500); }
        }
        if (!ok) { g_sm_fail = 1; return 0; }
    }
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
static volatile int g_reloc;   /* the player picked another game folder: look at the files again before anything is downloaded */
static int g_auto_retry;
static volatile LONG g_recheck;          /* the game was just closed: look for an update right away */
/* build 24 / 25: the owner published add-on v239 and players who closed their game were never asked to update - the launcher said "Up to
   date" and they pressed Play again. Cause: the monitor asked for the re-check BEFORE it set "Up to date" (and Steam's play time was
   read in between), so the check came back while the state was still RUNNING and was thrown away; the next one was 20 s later.
   Now: when the game closes the launcher says "Looking for updates..." (ST_CHECKING) until the check is back - an update shows at
   once (and downloads by itself, as before), otherwise "Up to date". Pressing Play also checks first (g_play_pending): with an
   update the download starts instead of the game; without one, or when the server doesn't answer, the game starts as before. */
static volatile LONG g_play_pending;     /* Play was pressed: start the game once the update check is back */
#define WM_PLAY_NOW (WM_APP + 0x79)

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
            if (!info && tls_error(g_net_err))
                set_status(ST_OFFLINE, L"Can't make a secure connection to the server (its certificate couldn't be verified). Nothing is downloaded until it can. Press Retry.");
            else
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
        if (need && !g_signed) {                 /* build 26: nothing is downloaded from a list the owner didn't sign */
            set_status(ST_ERROR, L"This update isn't signed by the server owner yet, so it isn't downloaded (that keeps your PC safe). Trying again in a minute...");
            wait_click(60);
            continue;
        }
        if (!need) {
            g_installed_once = 1;                    /* (also when the chosen folder already had the whole game in it) */
            save_state(); save_cache();
            set_status(ST_READY, L"Up to date \x2022 version %d", g_version);
            if (game_running()) start_monitor();
            /* keep the numbers fresh, and notice a new update while the launcher is open (like Steam) */
            for (int t = 0;; t++) {
                for (int w = 0; w < 200 && !g_recheck; w++) Sleep(100);   /* every 20 s, or at once when the game closes */
                InterlockedExchange(&g_recheck, 0);
                char *i2 = http_get(L"/launcher/info", &code);
                double nv = 0;
                if (i2 && code == 200 && json_num(i2, "lobbies", &lob) && json_num(i2, "players", &pl)) {
                    g_lobbies = (int)lob; g_players = (int)pl; g_online = 1;
                    PostMessage(g_wnd, WM_REFRESH, 0, 0);
                }
                int newer = i2 && code == 200 && json_num(i2, "version", &nv) && (int)nv != g_version;
                free(i2);
                if (newer && (g_state == ST_READY || g_state == ST_CHECKING)) { InterlockedExchange(&g_play_pending, 0); break; }     /* re-check files -> update */
                if (g_state == ST_CHECKING) {               /* build 24: the check after the game closed / after Play: nothing new */
                    set_status(ST_READY, L"Up to date \x2022 version %d", g_version);
                    if (InterlockedExchange(&g_play_pending, 0)) PostMessage(g_wnd, WM_PLAY_NOW, 0, 0);
                }
            }
            continue;
        }
        int cnt = 0; for (int i = 0; i < g_nfiles; i++) cnt += g_files[i].need;
        g_total = need; g_done = 0;
        set_status(ST_NEED, g_installed_once ? L"Update available \x2022 version %d \x2022 %d file(s), %.1f MB"
                                             : L"Ready to install \x2022 version %d \x2022 %d file(s), %.2f GB",
                   g_version, cnt, g_installed_once ? need / 1048576.0 : need / 1073741824.0);
        /* An update for a game that's already installed starts by itself (like Steam) - as soon as the game isn't running.
           The first install still waits for the player to press Install (it's big, and they may want another folder). */
        if (g_auto_retry) g_go = 0;
        else if (g_installed_once) { while (game_running() && !g_go) Sleep(1000); g_go = 0; }
        else wait_click(0);
        if (g_reloc) { g_reloc = 0; continue; }           /* another folder was chosen: check that one first */

        ULARGE_INTEGER freeb; wchar_t droot[4] = { g_game[0], L':', L'\\', 0 };
        if (GetDiskFreeSpaceExW(g_game[1] == L':' ? droot : g_base, &freeb, NULL, NULL) && (LONGLONG)freeb.QuadPart < need + (256LL << 20)) {
            set_status(ST_ERROR, L"Not enough disk space: %.1f GB free space needed. Free some space and press Retry.", need / 1073741824.0);
            wait_click(0);
            continue;
        }
        set_status(ST_DOWNLOADING, L"Downloading...");
        llog("launcher build %d: downloading %.2f GB (Cloudflare: %ls)", PSO_BUILD, need / 1073741824.0, g_cdn[0] ? g_cdn : L"none");
        int ok = 1;
        {   int nsmall = 0; for (int i = 0; i < g_nfiles; i++) if (g_files[i].need && g_files[i].s < PAR_MIN) nsmall++;
            if (nsmall > 1) {                                   /* the small files 8 at a time: far away, each one waits a whole round trip */
                set_status(-1, L"Downloading %d files", nsmall);
                g_sm_next = 0; g_sm_fail = 0;
                HANDLE th[8]; int nt = 0;
                for (int k = 0; k < 8 && k < nsmall; k++) { th[nt] = CreateThread(NULL, 0, small_thread, NULL, 0, NULL); if (th[nt]) nt++; }
                if (nt) WaitForMultipleObjects(nt, th, TRUE, INFINITE);
                for (int k = 0; k < nt; k++) CloseHandle(th[k]);
                if (g_sm_fail) ok = 0;
            }
        }
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
static int is_wine(void);
static int unix_steam_running(void) {                                   /* Linux under Wine: the real Steam is a Linux program, seen in /proc */
    WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(L"Z:\\proc\\*", &fd); int found = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] < L'0' || fd.cFileName[0] > L'9') continue;
        wchar_t p[64]; swprintf(p, 64, L"Z:\\proc\\%ls\\comm", fd.cFileName);
        HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        char b[32] = ""; DWORD got = 0; ReadFile(h, b, sizeof b - 1, &got, NULL); CloseHandle(h); b[got] = 0;
        if (!strncmp(b, "steam\n", 6) || !strncmp(b, "steamwebhelper", 14) || !strncmp(b, "steam_osx", 9)) found = 1;
    } while (!found && FindNextFileW(f, &fd));
    FindClose(f);
    return found;
}
static int steam_running(void) {
    if (is_wine() && unix_steam_running()) return 1;
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
    set_status(ST_CHECKING, L"Looking for updates...");   /* build 24: not "Up to date" before the check is back (the worker sets it) */
    InterlockedExchange(&g_recheck, 1);           /* a new update may have come out while they played: get it now */
    PostMessage(g_wnd, WM_APP + 0x77, 0, 0);      /* game closed: show the launcher again */
    steam_refresh();                              /* new play time from Steam */
    return 0;
}
static void start_monitor(void) {
    if (g_monitoring) return;
    g_monitoring = 1;
    CloseHandle(CreateThread(NULL, 0, monitor, NULL, 0, NULL));
}
static void play_now(void);
static void play(void) {                          /* build 24: Play looks for an update first (the worker answers with WM_PLAY_NOW or an update) */
    if (g_state == ST_READY && !game_running() && !g_monitoring) {
        InterlockedExchange(&g_play_pending, 1); set_status(ST_CHECKING, L"Looking for updates..."); InterlockedExchange(&g_recheck, 1); return; }
    play_now();
}
static void play_now(void) {
    if (game_running()) { if (!g_stay_open) ShowWindow(g_wnd, SW_HIDE); start_monitor(); return; }
    if (!steam_running()) {
        int a = MessageBoxW(g_wnd, L"Steam isn't running.\n\n"
                                   L"Yes  -  open Steam (then press Play again when it's ready)\n"
                                   L"No  -  play without Steam (Mac or Linux without Steam: GameHub, CrossOver, Wine)",
                            L"PSO UE4 Launcher", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (a == IDYES) { ShellExecuteW(NULL, L"open", L"steam://open/main", NULL, NULL, SW_SHOWNORMAL); return; }
        if (a != IDNO) return;
    }
    wchar_t exe[MAX_PATH * 2]; swprintf(exe, MAX_PATH * 2, L"%ls\\ProSoccerOnline.exe", g_game);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, g_game, &si, &pi)) {
        MessageBoxW(g_wnd, L"Could not start the game. Restart the launcher to repair the files.", L"PSO UE4 Launcher", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (!g_stay_open) ShowWindow(g_wnd, SW_HIDE);  /* game started: the launcher gets out of the way (unless the player wants it kept open) ... */
    start_monitor();                               /* ... and comes back when the game is closed */
}

/* launcher build number: the game add-on only replaces a launcher with a higher one */
#ifndef PSO_BUILD
#endif
#define PSO_STR2(x) #x
#define PSO_STR(x) PSO_STR2(x)
/* the one place this text may appear in the program: the game add-on reads the FIRST "PSOLAUNCHER_BUILD=" it finds in a launcher file */
__attribute__((used)) static const char g_build_tag[] = "PSOLAUNCHER_BUILD=" PSO_STR(PSO_BUILD) ";";
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
/* The look follows the website: near-black, a field of small moving dots, glass panels, white pill buttons,
 * Space Grotesk for headings / labels / buttons (carried inside this exe) and Segoe UI for running text. */
int WINAPI GdipFillEllipse(void *, void *, float, float, float, float);
int WINAPI GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, void **);
int WINAPI GdipGetImageGraphicsContext(void *, void **);
int WINAPI GdipDrawImageRectI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipSetClipRectI(void *, INT, INT, INT, INT, int);
BOOL WINAPI GdiAlphaBlend(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
#include "fonts.h"
static int g_dpi = 96;
#define S(v) MulDiv((v), g_dpi, 96)
static int g_fonts_ok;
static HFONT f_btn, f_btn2, f_lbl, f_val, f_text, f_small, f_h2, f_big, f_valb, f_title, f_splash, f_splash2;
static HFONT mkfont(int px, int weight, const wchar_t *face) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
#define F_BODY L"Segoe UI"
static const wchar_t *f_disp(int bold) { return !g_fonts_ok ? F_BODY : bold ? L"Space Grotesk" : L"Space Grotesk Medium"; }
static void make_fonts(void) {
    HFONT *all[] = { &f_btn, &f_btn2, &f_lbl, &f_val, &f_text, &f_small, &f_h2, &f_big, &f_valb, &f_title, &f_splash, &f_splash2 };
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) if (*all[i]) DeleteObject(*all[i]);
    f_btn = mkfont(15, 700, f_disp(1)); f_btn2 = mkfont(13, 700, f_disp(1)); f_lbl = mkfont(11, g_fonts_ok ? 500 : 700, f_disp(0));
    f_val = mkfont(17, g_fonts_ok ? 500 : 600, f_disp(0)); f_valb = mkfont(15, 600, F_BODY); f_text = mkfont(14, 400, F_BODY); f_small = mkfont(12, 400, F_BODY);
    f_h2 = mkfont(17, 700, f_disp(1)); f_big = mkfont(30, 700, f_disp(1)); f_title = mkfont(12, 700, f_disp(1));
    f_splash = mkfont(30, 700, f_disp(1)); f_splash2 = mkfont(12, g_fonts_ok ? 500 : 600, f_disp(0));
}

/* ------------------------------------------------------------ look */
#define C_BG    RGB(5, 5, 5)
#define C_TEXT  RGB(228, 228, 231)
#define C_DIM   RGB(161, 161, 170)
#define C_LBL   RGB(138, 138, 148)
#define C_GREEN RGB(120, 226, 140)
#define C_RED   RGB(248, 113, 113)
#define C_BLUE  RGB(96, 165, 250)

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, UINT fl) {
    RECT r = { x, y, x + w, y + h }; SelectObject(dc, f); SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
}
static int text_w(HDC dc, HFONT f, const wchar_t *s) {
    SIZE sz; SelectObject(dc, f); GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz); return sz.cx;
}
/* the same with the letters spread apart (small capital labels, buttons) */
static void text_sp(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, UINT fl, int extra) {
    SetTextCharacterExtra(dc, extra); text(dc, f, c, x, y, w, h, s, fl); SetTextCharacterExtra(dc, 0);
}
static int text_w_sp(HDC dc, HFONT f, const wchar_t *s, int extra) {
    SetTextCharacterExtra(dc, extra); int w = text_w(dc, f, s) - extra; SetTextCharacterExtra(dc, 0); return w;
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
typedef struct { int W, H, M, tb; RECT hero, act, play, bmin, bclose, notes, server, title, gear, change, set, sclose, tog[2], sfolder, slink; } Layout;
static Layout L;
static void layout_wh(int cw, int ch);
static void calc_layout(HWND h) { RECT rc; GetClientRect(h, &rc); layout_wh(rc.right, rc.bottom); }
static void layout_wh(int cw, int ch) {
    L.W = cw; L.H = ch; L.M = S(20); L.tb = S(58);
    int heroH = (L.W - 2 * L.M) * 10 / 38;                         /* wide banner */
    int maxH = L.H - L.tb - S(14) - S(88) - S(14) - S(200) - L.M;   /* leave room for the cards */
    if (heroH > maxH) heroH = maxH; if (heroH > S(380)) heroH = S(380); if (heroH < S(160)) heroH = S(160);
    SetRect(&L.hero, L.M, L.tb, L.W - L.M, L.tb + heroH);
    SetRect(&L.act, L.M, L.hero.bottom + S(14), L.W - L.M, L.hero.bottom + S(14) + S(88));
    SetRect(&L.play, L.act.left + S(20), L.act.top + S(20), L.act.left + S(20) + S(200), L.act.top + S(20) + S(48));
    SetRect(&L.bclose, L.W - L.M - S(32), S(13), L.W - L.M, S(45));
    SetRect(&L.bmin, L.W - L.M - S(72), S(13), L.W - L.M - S(40), S(45));
    SetRect(&L.title, L.M, S(13), L.M + S(260), S(45));
    SetRect(&L.gear, L.W - L.M - S(112), S(13), L.W - L.M - S(80), S(45));
    SetRect(&L.change, L.act.right - S(274) - S(22) - S(86), L.act.top + S(30), L.act.right - S(274) - S(22), L.act.top + S(58));
    {   /* the settings card, in the middle of the window */
        int cw = S(520), ch = S(392); if (cw > L.W - 2 * L.M) cw = L.W - 2 * L.M; if (ch > L.H - 2 * L.M) ch = L.H - 2 * L.M;
        int x = (L.W - cw) / 2, y = (L.H - ch) / 2;
        SetRect(&L.set, x, y, x + cw, y + ch);
        SetRect(&L.sclose, x + cw - S(48), y + S(13), x + cw - S(18), y + S(43));
        for (int i = 0; i < 2; i++) SetRect(&L.tog[i], x + cw - S(24) - S(44), y + S(76) + i * S(70), x + cw - S(24), y + S(76) + i * S(70) + S(24));
        SetRect(&L.slink, x + cw - S(24) - S(100), y + S(76) + 2 * S(70) - S(2), x + cw - S(24), y + S(76) + 2 * S(70) + S(26));
        SetRect(&L.sfolder, x + cw - S(24) - S(78), y + S(76) + 3 * S(70) - S(2), x + cw - S(24), y + S(76) + 3 * S(70) + S(26));
    }
    int cy = L.act.bottom + S(14), rw = S(330);
    SetRect(&L.notes, L.M, cy, L.W - L.M - rw - S(14), L.H - L.M);
    SetRect(&L.server, L.W - L.M - rw, cy, L.W - L.M, L.H - L.M);
}
static int in_rect(const RECT *r, int x, int y) { return x >= r->left && x < r->right && y >= r->top && y < r->bottom; }

/* ------------------------------------------------------------ interaction state */
enum { HOT_NONE, HOT_PLAY, HOT_MIN, HOT_CLOSE, HOT_GEAR, HOT_CHANGE, HOT_SCLOSE, HOT_TOG1, HOT_TOG2, HOT_SFOLDER, HOT_SLINK, HOT_SOUT };
/* Discord link (PUGs): 0 nothing yet, 1 waiting for the Steam sign-in in the browser, 2 checking, 3 code ready, 4 a message */
static volatile int g_link_state;
static wchar_t g_link_text[300];
static wchar_t g_link_code[16];
static int g_settings;               /* the settings card is open */
/* before the first install the play bar shows where the game will go, with a button to change it */
static int loc_shown(void) { return !g_installed_once && g_state != ST_DOWNLOADING && !(g_state == ST_NEED && g_done > 0); }
static int g_hot, g_pressed, g_scroll, g_content_h;
static double g_speed;
static LONGLONG g_last_done;
static float g_spin;                 /* update-arrow angle (degrees) */
static int spinning(void) { return g_state == ST_DOWNLOADING || g_state == ST_CHECKING || g_state == ST_CONNECTING || g_state == ST_WAITING; }

/* ------------------------------------------------------------ movement: the opening and the dot field */
static double now_s(void) {
    static LARGE_INTEGER f, t0; LARGE_INTEGER t;
    if (!f.QuadPart) { QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0); }
    QueryPerformanceCounter(&t); return (double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart;
}
#define INTRO_END 2.40                /* seconds: logo in, name in, logo flies to its place, the panels rise */
#define OUTRO_END 0.44                /* closing: the panels sink away one after the other while the window fades */
static int g_motion = 1, g_intro_on, g_outro_on; static double g_intro0, g_outro0, g_shot_out = -1;
static double outro_t(void) { return g_shot_out >= 0 ? g_shot_out : now_s() - g_outro0; }
static double g_shot_t = -1;          /* preview builds only: a fixed moment of the opening */
static double intro_t(void) { return g_shot_t >= 0 ? g_shot_t : g_intro_on ? now_s() - g_intro0 : 99.0; }
static float ease(double p) { if (p <= 0) return 0; if (p >= 1) return 1; double q = 1 - p; return (float)(1 - q * q * q * q); }
static float seg(double t, double start, double dur) { return ease((t - start) / dur); }

typedef struct { float bx, by, x, y; } Dot;
static Dot *g_dots; static int g_ndots; static float g_wave;
static POINT g_mouse = { -10000, -10000 }; static int g_mouse_in;
static void dots_setup(void) {
    int gap = S(35), nx = L.W / gap + 3, ny = L.H / gap + 3, n = nx * ny, k = 0;
    if (n <= 0 || n > 20000) { g_ndots = 0; return; }
    Dot *d = realloc(g_dots, sizeof(Dot) * n); if (!d) { g_ndots = 0; return; }
    g_dots = d;
    for (int ix = 0; ix < nx; ix++) for (int iy = 0; iy < ny; iy++) { Dot *p = &g_dots[k++]; p->bx = p->x = (float)((ix - 1) * gap); p->by = p->y = (float)((iy - 1) * gap); }
    g_ndots = k;
}
static void dots_step(double dt) {                                      /* dt seconds on: a slow wave, and the dots lean away from the mouse */
    float amp = (float)S(12), reach = (float)S(190), push = (float)S(60), k = 0.003f * 96.0f / (float)g_dpi;
    float follow = 1.0f - expf(-5.3f * (float)dt);
    g_wave += 0.32f * (float)dt;
    for (int i = 0; i < g_ndots; i++) { Dot *p = &g_dots[i];
        float tx = p->bx + sinf(p->bx * k + g_wave) * amp, ty = p->by + cosf(p->by * k + g_wave) * amp;
        if (g_mouse_in) { float dx = (float)g_mouse.x - tx, dy = (float)g_mouse.y - ty, d = sqrtf(dx * dx + dy * dy);
            if (d < reach && d > 0.01f) { float f = (reach - d) / reach, e = f * f * (3 - 2 * f); tx -= dx / d * e * push; ty -= dy / d * e * push; } }
        p->x += (tx - p->x) * follow; p->y += (ty - p->y) * follow; }
}
/* The dots and the opening's soft light are written straight into the frame's pixels: a few hundred tiny soft discs
 * cost next to nothing this way, and they can sit between two pixels, so slow movement stays smooth.
 * px = the frame (32-bit, top row first), hide = the boxes that cover the background (the panels). */
static void px_add(uint32_t *q, int v) {
    uint32_t c = *q; int b = (int)(c & 255) + v, g = (int)((c >> 8) & 255) + v, r = (int)((c >> 16) & 255) + v;
    if (b > 255) b = 255; if (g > 255) g = 255; if (r > 255) r = 255;
    *q = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static void dots_px(uint32_t *px, int W, int H, float alpha, const RECT *hide, int nhide) {
    if (!px || alpha <= 0.01f || !g_ndots) return;
    float r = 0.55f + 0.55f * (float)g_dpi / 96.0f, lum = 52.0f * alpha; int pad = (int)r + 2;
    for (int i = 0; i < g_ndots; i++) { float fx = g_dots[i].x, fy = g_dots[i].y; int x = (int)fx, y = (int)fy, in = 0;
        if (x < pad || y < pad || x >= W - pad || y >= H - pad) continue;
        for (int k = 0; k < nhide && !in; k++) in = x >= hide[k].left - pad && x < hide[k].right + pad && y >= hide[k].top - pad && y < hide[k].bottom + pad;
        if (in) continue;
        for (int yy = y - pad + 1; yy <= y + pad; yy++) for (int xx = x - pad + 1; xx <= x + pad; xx++) {
            float dx = (float)xx + 0.5f - fx, dy = (float)yy + 0.5f - fy, cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            px_add(px + (size_t)yy * W + xx, (int)(lum * cov + 0.5f)); } }
}
static void glow_px(uint32_t *px, int W, int H, float cx, float cy, float R, float peak) {      /* a soft round light, brightest in the middle */
    if (!px || peak < 0.5f || R < 2) return;
    int x0 = (int)(cx - R), x1 = (int)(cx + R) + 1, y0 = (int)(cy - R), y1 = (int)(cy + R) + 1;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy) / R;
        if (d >= 1) continue; float f = 1 - d; int v = (int)(peak * f * f + 0.5f); if (v > 0) px_add(px + (size_t)y * W + x, v); }
}

/* circular "refresh" arrow, rotated by deg, centred at cx,cy */
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
static int btn_enabled(void) { return !g_updating && (g_state == ST_NEED || g_state == ST_READY || g_state == ST_OFFLINE || g_state == ST_ERROR || g_state == ST_WAITING); }

static void hgrad(void *G, int x, int y, int w, int h, DWORD left, DWORD right) {
    if (w <= 0) return;
    void *b; GpPointI a = { x - 1, y }, z = { x + w + 1, y };
    GdipCreateLineBrushI(&a, &z, left, right, 0, &b);
    GdipFillRectangleI(G, b, x, y, w, h); GdipDeleteBrush(b);
}
/* glass panel: almost-black fill, hairline border, a soft light along the top edge, drop shadow */
static void glass(void *G, RECT r, int rad) {
    int w = r.right - r.left, h = r.bottom - r.top;
    rr_grad(G, r.left, r.top, w, h, rad, ARGB(255, 16, 16, 17), ARGB(255, 9, 9, 10));
    rr_line(G, r.left, r.top, w, h, rad, ARGB(24, 255, 255, 255));
    int x0 = r.left + rad, x1 = r.right - rad, mid = (x0 + x1) / 2;
    hgrad(G, x0, r.top, mid - x0, 1, ARGB(0, 255, 255, 255), ARGB(46, 255, 255, 255));
    hgrad(G, mid, r.top, x1 - mid, 1, ARGB(46, 255, 255, 255), ARGB(0, 255, 255, 255));
}
static void dot(void *G, int x, int y, int d, DWORD c) { void *b; GdipCreateSolidFill(c, &b); GdipFillEllipseI(G, b, x, y, d, d); GdipDeleteBrush(b); }

/* ------------------------------------------------------------ pieces */
static int g_title_w;                /* how wide the title pill came out */
static void title_logo_rect(RECT *o) { RECT t = L.title; int h = t.bottom - t.top, lg = S(20); SetRect(o, t.left + S(12), t.top + (h - lg) / 2, t.left + S(12) + lg, t.top + (h - lg) / 2 + lg); }
static void draw_title(HDC m, void *G) {
    RECT t = L.title, lr; int h = t.bottom - t.top, sp = S(2);
    int w1 = text_w_sp(m, f_title, L"PSO UE4", sp), w2 = text_w_sp(m, f_title, L"LAUNCHER", sp);
    title_logo_rect(&lr);
    int lg = lr.right - lr.left, tx = lr.right + S(10), w = (tx - t.left) + w1 + S(8) + w2 + S(18);
    g_title_w = w;
    rr_fill(G, t.left, t.top, w, h, h / 2, ARGB(255, 9, 9, 10));
    rr_line(G, t.left, t.top, w, h, h / 2, ARGB(26, 255, 255, 255));
    EnterCriticalSection(&g_imgcs);
    if (g_logo) GdipDrawImageRectRectI(G, g_logo, lr.left, lr.top, lg, lg, 0, 0, 128, 128, 2, NULL, NULL, NULL);
    LeaveCriticalSection(&g_imgcs);
    text_sp(m, f_title, RGB(255, 255, 255), tx, t.top, w1 + S(8), h, L"PSO UE4", DT_SINGLELINE | DT_VCENTER, sp);
    text_sp(m, f_title, C_DIM, tx + w1 + S(8), t.top, w2 + S(8), h, L"LAUNCHER", DT_SINGLELINE | DT_VCENTER, sp);
}

static void draw_window_buttons(void *G) {
    RECT bs[3] = { L.gear, L.bmin, L.bclose }; int ids[3] = { HOT_GEAR, HOT_MIN, HOT_CLOSE };
    for (int i = 0; i < 3; i++) {
        RECT r = bs[i]; int w = r.right - r.left, h = r.bottom - r.top, hot = g_hot == ids[i];
        rr_fill(G, r.left, r.top, w, h, h / 2, hot ? (i == 2 ? ARGB(255, 220, 38, 38) : ARGB(255, 48, 48, 50)) : ARGB(255, 9, 9, 10));
        rr_line(G, r.left, r.top, w, h, h / 2, ARGB(hot ? 70 : 26, 255, 255, 255));
        void *pen; GdipCreatePen1(ARGB(hot ? 255 : 200, 255, 255, 255), (float)S(2) * 0.7f, 2, &pen);
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, k = S(4);
        if (i == 0) {        /* settings: two sliders */
            int q = S(6); GdipDrawLineI(G, pen, cx - q, cy - S(3), cx + q, cy - S(3)); GdipDrawLineI(G, pen, cx - q, cy + S(3), cx + q, cy + S(3));
            DWORD bgc = hot ? ARGB(255, 48, 48, 50) : ARGB(255, 9, 9, 10);
            dot(G, cx + S(1), cy - S(6), S(6), ARGB(255, 255, 255, 255)); dot(G, cx + S(2) + 1, cy - S(5) + 1, S(4) - 2, bgc);
            dot(G, cx - S(6), cy, S(6), ARGB(255, 255, 255, 255)); dot(G, cx - S(5) + 1, cy + S(1) + 1, S(4) - 2, bgc);
        }
        else if (i == 1) GdipDrawLineI(G, pen, cx - k, cy, cx + k, cy);
        else { GdipDrawLineI(G, pen, cx - k, cy - k, cx + k, cy + k); GdipDrawLineI(G, pen, cx - k, cy + k, cx + k, cy - k); }
        GdipDeletePen(pen);
    }
}

/* the main button, a pill: white = go (Play, first Install), blue = an update is waiting, glass = busy or retry */
static int g_arrow_on; static float g_arrow_cx, g_arrow_cy; static DWORD g_arrow_col;
static void draw_play_button(HDC m, void *G) {
    g_arrow_on = 0;
    RECT r = L.play; int w = r.right - r.left, h = r.bottom - r.top, en = btn_enabled(), rad = h / 2;
    int hot = en && g_hot == HOT_PLAY, down = hot && g_pressed == HOT_PLAY;
    int busy = g_state == ST_RUNNING || g_state == ST_DOWNLOADING || g_state == ST_CHECKING || g_state == ST_CONNECTING;
    int kind = (g_state == ST_READY || (g_state == ST_NEED && !g_installed_once)) ? 0 : (g_state == ST_NEED) ? 1 : 2;
    COLORREF tc; DWORD ic;
    if (kind == 0) {
        int v = down ? 222 : hot ? 255 : 244;
        if (hot) for (int i = 5; i >= 1; i--) rr_fill(G, r.left - S(i * 2), r.top - S(i * 2), w + S(i * 4), h + S(i * 4), rad + S(i * 2), ARGB(40 / (i + 1), 255, 255, 255));
        rr_fill(G, r.left, r.top, w, h, rad, ARGB(255, v, v, v));
        tc = RGB(0, 0, 0); ic = ARGB(255, 0, 0, 0);
    } else if (kind == 1) {
        DWORD top = down ? ARGB(255, 37, 99, 235) : hot ? ARGB(255, 120, 180, 255) : ARGB(255, 96, 165, 250);
        DWORD bot = down ? ARGB(255, 29, 78, 216) : hot ? ARGB(255, 59, 130, 246) : ARGB(255, 37, 99, 235);
        for (int i = 4; i >= 1; i--) rr_fill(G, r.left - S(i * 2), r.top - S(i * 2) + S(2), w + S(i * 4), h + S(i * 4), rad + S(i * 2), ARGB((hot ? 90 : 56) / (i + 1), 59, 130, 246));
        rr_grad(G, r.left, r.top, w, h, rad, top, bot);
        rr_line(G, r.left, r.top, w, h, rad, ARGB(60, 255, 255, 255));
        tc = RGB(255, 255, 255); ic = ARGB(255, 255, 255, 255);
    } else {
        rr_fill(G, r.left, r.top, w, h, rad, ARGB(en ? (down ? 14 : hot ? 34 : 16) : 8, 255, 255, 255));
        rr_line(G, r.left, r.top, w, h, rad, ARGB(en ? (hot ? 90 : 50) : 26, 255, 255, 255));
        tc = (en || busy) ? RGB(255, 255, 255) : C_DIM; ic = (en || busy) ? ARGB(255, 255, 255, 255) : ARGB(255, 161, 161, 170);
    }
    const wchar_t *t = btn_text();
    HFONT f = wcslen(t) > 8 ? f_btn2 : f_btn; int sp = S(1), cy = r.top + h / 2;
    int tw = text_w_sp(m, f, t, sp);
    if (g_state == ST_READY || g_state == ST_RUNNING) {
        int iw = S(g_state == ST_RUNNING ? 15 : 13), gap = S(12), x0 = r.left + (w - (iw + gap + tw)) / 2;
        void *b; GdipCreateSolidFill(ic, &b);
        if (g_state == ST_RUNNING) {         /* two bars = in game */
            GdipFillRectangleI(G, b, x0, cy - S(8), S(5), S(16)); GdipFillRectangleI(G, b, x0 + S(10), cy - S(8), S(5), S(16));
        } else {
            GpPointI tri[3] = { { x0, cy - S(8) }, { x0, cy + S(8) }, { x0 + iw, cy } };
            GdipFillPolygonI(G, b, tri, 3, 0);
        }
        GdipDeleteBrush(b);
        text_sp(m, f, tc, x0 + iw + gap, r.top, tw + S(8), h, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sp);
    } else {
        int iw = S(18), gap = S(10), x0 = r.left + (w - (iw + gap + tw)) / 2;      /* arrow + text (the arrow turns while working) */
        g_arrow_on = 1; g_arrow_cx = (float)(x0 + iw / 2); g_arrow_cy = (float)cy; g_arrow_col = ic;
        if (!spinning()) draw_update_arrow(G, g_arrow_cx, g_arrow_cy, (float)S(7), 0.0f, ic);      /* while it turns it is drawn per frame, see compose() */
        text_sp(m, f, tc, x0 + iw + gap, r.top, tw + S(8), h, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sp);
    }
}

static void icon_clock(void *G, int x, int y) {
    void *pen; GdipCreatePen1(ARGB(255, 161, 161, 170), (float)S(2) * 0.8f, 2, &pen);
    GdipDrawEllipseI(G, pen, x, y, S(24), S(24));
    GdipDrawLineI(G, pen, x + S(12), y + S(6), x + S(12), y + S(12));
    GdipDrawLineI(G, pen, x + S(12), y + S(12), x + S(17), y + S(15));
    GdipDeletePen(pen);
}
static void icon_calendar(void *G, int x, int y) {
    void *pen; GdipCreatePen1(ARGB(255, 161, 161, 170), (float)S(2) * 0.8f, 2, &pen);
    void *p = rr_path(x, y + S(3), S(24), S(21), S(5)); GdipDrawPath(G, pen, p); GdipDeletePath(p);
    GdipDrawLineI(G, pen, x, y + S(10), x + S(24), y + S(10));
    GdipDrawLineI(G, pen, x + S(7), y, x + S(7), y + S(6)); GdipDrawLineI(G, pen, x + S(17), y, x + S(17), y + S(6));
    GdipDeletePen(pen);
}
static void icon_folder(void *G, int x, int y) {
    void *pen; GdipCreatePen1(ARGB(255, 161, 161, 170), (float)S(2) * 0.8f, 2, &pen);
    void *p = rr_path(x, y + S(5), S(24), S(18), S(4)); GdipDrawPath(G, pen, p); GdipDeletePath(p);
    GdipDrawLineI(G, pen, x + S(3), y + S(5), x + S(3), y + S(2)); GdipDrawLineI(G, pen, x + S(3), y + S(2), x + S(10), y + S(2)); GdipDrawLineI(G, pen, x + S(10), y + S(2), x + S(13), y + S(5));
    GdipDeletePen(pen);
}
static void vsep(void *G, int x, int y, int h) {
    void *b; GdipCreateSolidFill(ARGB(22, 255, 255, 255), &b); GdipFillRectangleI(G, b, x, y, 1, h); GdipDeleteBrush(b);
}

static void draw_action(HDC m, void *G) {
    glass(G, L.act, S(22));
    draw_play_button(m, G);
    int ay = L.act.top - S(6), sx = L.play.right + S(32), right_limit = L.act.right - S(274), lsp = S(1);
    EnterCriticalSection(&g_cs);
    if (g_state == ST_DOWNLOADING || (g_state == ST_NEED && g_done > 0)) {
        double fr = g_total ? (double)g_done / g_total : 0; if (fr > 1) fr = 1;
        int pw = right_limit - sx;
        wchar_t sp[96] = L"";
        if (g_state == ST_DOWNLOADING && g_speed > 1) {
            double left = (g_total - g_done) / g_speed;
            if (left < 60) swprintf(sp, 96, L"%.1f MB/s  \x2022  %d sec left", g_speed / 1048576.0, (int)left + 1);
            else swprintf(sp, 96, L"%.1f MB/s  \x2022  %d min left", g_speed / 1048576.0, (int)(left / 60) + 1);
        }
        int spw = sp[0] ? text_w(m, f_small, sp) + S(14) : 0;               /* the file name stops where the speed begins */
        text(m, f_valb, C_TEXT, sx, ay + S(22), pw - spw, S(24), g_status, DT_SINGLELINE | DT_END_ELLIPSIS);
        text(m, f_small, C_DIM, sx, ay + S(25), pw, S(20), sp, DT_SINGLELINE | DT_RIGHT);
        rr_fill(G, sx, ay + S(55), pw, S(8), S(4), ARGB(255, 30, 30, 34));
        int fw = (int)(pw * fr); if (fw > 0 && fw < S(8)) fw = S(8);
        if (fw > 0) {
            rr_fill(G, sx - S(2), ay + S(52), fw + S(4), S(14), S(7), ARGB(26, 255, 255, 255));
            rr_grad(G, sx, ay + S(55), fw, S(8), S(4), ARGB(255, 255, 255, 255), ARGB(255, 196, 196, 204));
        }
        wchar_t t[96]; swprintf(t, 96, L"%.2f / %.2f GB   %d%%", g_done / 1073741824.0, g_total / 1073741824.0, (int)(fr * 100));
        text(m, f_small, C_DIM, sx, ay + S(70), pw, S(20), t, DT_SINGLELINE);
    } else if (loc_shown()) {
        /* where the game will be installed, and a way to choose another place */
        int col = sx, can = g_state == ST_NEED, hot = can && g_hot == HOT_CHANGE, down = hot && g_pressed == HOT_CHANGE;
        wchar_t lbl[96] = L"INSTALL LOCATION", root[4] = { g_game[0], L':', L'\\', 0 }; ULARGE_INTEGER fr;
        if (g_game[1] == L':' && GetDiskFreeSpaceExW(root, &fr, NULL, NULL)) swprintf(lbl, 96, L"INSTALL LOCATION  \x2022  %.0f GB FREE", fr.QuadPart / 1073741824.0);
        vsep(G, col - S(16), L.act.top + S(20), S(48));
        icon_folder(G, col, ay + S(38));
        text_sp(m, f_lbl, C_LBL, col + S(38), ay + S(30), L.change.left - S(14) - (col + S(38)), S(18), lbl, DT_SINGLELINE | DT_END_ELLIPSIS, lsp);
        text(m, f_valb, RGB(255, 255, 255), col + S(38), ay + S(49), L.change.left - S(14) - (col + S(38)), S(24), g_game, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        RECT c = L.change; int cw = c.right - c.left, ch = c.bottom - c.top;
        rr_fill(G, c.left, c.top, cw, ch, ch / 2, ARGB(can ? (down ? 14 : hot ? 40 : 18) : 8, 255, 255, 255));
        rr_line(G, c.left, c.top, cw, ch, ch / 2, ARGB(can ? (hot ? 100 : 54) : 24, 255, 255, 255));
        text_sp(m, f_lbl, can ? RGB(255, 255, 255) : C_LBL, c.left + S(1), c.top, cw, ch, L"CHANGE", DT_CENTER | DT_VCENTER | DT_SINGLELINE, S(1));
    } else {
        wchar_t v[64]; int col = sx;
        vsep(G, col - S(16), L.act.top + S(20), S(48));
        icon_calendar(G, col, ay + S(38));
        text_sp(m, f_lbl, C_LBL, col + S(38), ay + S(30), S(160), S(18), L"LAST PLAYED", DT_SINGLELINE, lsp);
        if (g_steam.ok) fmt_last(g_steam.last_played, v, 64); else wcscpy(v, L"\x2014");
        text(m, f_val, RGB(255, 255, 255), col + S(38), ay + S(48), S(160), S(28), v, DT_SINGLELINE);
        col += S(38) + S(160);
        vsep(G, col - S(16), L.act.top + S(20), S(48));
        icon_clock(G, col, ay + S(38));
        text_sp(m, f_lbl, C_LBL, col + S(38), ay + S(30), S(200), S(18), L"PLAY TIME", DT_SINGLELINE, lsp);
        if (g_steam.ok && g_steam.minutes >= 0) fmt_hours(g_steam.minutes, v, 64); else wcscpy(v, L"\x2014");
        text(m, f_val, RGB(255, 255, 255), col + S(38), ay + S(48), S(220), S(28), v, DT_SINGLELINE);
    }
    LeaveCriticalSection(&g_cs);

    /* Steam account */
    int ax = L.act.right - S(252), avy = L.act.top + S(21), av = S(46);
    vsep(G, ax - S(22), L.act.top + S(20), S(48));
    if (g_steam.ok) {
        dot(G, ax - S(3), avy - S(3), av + S(6), ARGB(70, 255, 255, 255));
        dot(G, ax - S(1), avy - S(1), av + S(2), ARGB(255, 5, 5, 5));
        void *clip; GdipCreatePath(0, &clip); GdipAddPathEllipseI(clip, ax, avy, av, av);
        GdipSetClipPath(G, clip, 0);
        void *ph; GdipCreateSolidFill(ARGB(255, 32, 32, 36), &ph); GdipFillEllipseI(G, ph, ax, avy, av, av); GdipDeleteBrush(ph);
        EnterCriticalSection(&g_imgcs); draw_img(G, g_avatar, ax, avy, av, av); LeaveCriticalSection(&g_imgcs);
        GdipResetClip(G); GdipDeletePath(clip);
        text(m, f_valb, RGB(255, 255, 255), ax + av + S(14), avy + S(2), S(180), S(24), g_steam.name, DT_SINGLELINE | DT_END_ELLIPSIS);
        dot(G, ax + av + S(15), avy + S(33), S(7), ARGB(255, 120, 226, 140));
        text(m, f_small, C_GREEN, ax + av + S(28), avy + S(27), S(170), S(20), g_state == ST_RUNNING ? L"In game" : L"Connected with Steam", DT_SINGLELINE);
    } else {
        text(m, f_valb, C_DIM, ax, avy + S(2), S(240), S(24), L"Steam not found", DT_SINGLELINE);
        text(m, f_small, C_LBL, ax, avy + S(28), S(240), S(20), L"Open Steam to see your stats", DT_SINGLELINE);
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
            int ix = bullet ? S(20) : 0;
            const wchar_t *body = bullet ? t + 2 : t;
            RECT r = { x + ix, cy, x + w, cy + 10000 };
            SelectObject(m, f);
            DrawTextW(m, body, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            int lh = r.bottom - r.top;
            if (!measure) {
                if (bullet) {
                    void *G2; GdipCreateFromHDC(m, &G2); GdipSetSmoothingMode(G2, 4);
                    dot(G2, x + S(5), cy + S(7), S(5), ARGB(150, 255, 255, 255)); GdipDeleteGraphics(G2);
                }
                r.right = x + w; SetTextColor(m, first ? RGB(255, 255, 255) : RGB(212, 212, 216));
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

static void card_header(HDC m, void *G, RECT r, const wchar_t *title, DWORD accent) {
    dot(G, r.left + S(22), r.top + S(21), S(7), accent);
    text_sp(m, f_lbl, RGB(228, 228, 231), r.left + S(38), r.top + S(14), S(220), S(22), title, DT_SINGLELINE | DT_VCENTER, S(2));
}
static void pill(HDC m, void *G, int right, int top, const wchar_t *s, COLORREF tc, DWORD fill, DWORD line, DWORD dotc) {
    int sp = S(1), tw = text_w_sp(m, f_lbl, s, sp), lead = dotc ? S(15) : 0, w = tw + S(24) + lead, h = S(24), x = right - w;
    rr_fill(G, x, top, w, h, h / 2, fill); rr_line(G, x, top, w, h, h / 2, line);
    if (dotc) dot(G, x + S(11), top + S(8), S(7), dotc);
    text_sp(m, f_lbl, tc, x + S(12) + lead, top, tw + S(10), h, s, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sp);
}

static void draw_cards(HDC m, void *G) {
    RECT n = L.notes; int nw = n.right - n.left, nh = n.bottom - n.top;
    if (nh > S(70)) {
        glass(G, n, S(22));
        card_header(m, G, n, L"PATCH NOTES", ARGB(255, 255, 255, 255));
        if (g_version) { wchar_t v[32]; swprintf(v, 32, L"VERSION %d", g_version);
            pill(m, G, n.right - S(20), n.top + S(13), v, RGB(228, 228, 231), ARGB(12, 255, 255, 255), ARGB(34, 255, 255, 255), 0); }
        void *b; GdipCreateSolidFill(ARGB(18, 255, 255, 255), &b); GdipFillRectangleI(G, b, n.left + S(22), n.top + S(50), nw - S(44), 1); GdipDeleteBrush(b);
        int tx = n.left + S(24), ty = n.top + S(64), tw = nw - S(56), th = nh - S(78);
        g_content_h = draw_notes_text(m, tx, 0, tw, 1);
        int maxs = g_content_h - th; if (maxs < 0) maxs = 0;
        if (g_scroll > maxs) g_scroll = maxs; if (g_scroll < 0) g_scroll = 0;
        SaveDC(m); IntersectClipRect(m, tx, ty, tx + tw, ty + th);
        draw_notes_text(m, tx, ty - g_scroll, tw, 0);
        RestoreDC(m, -1);
        if (maxs > 0) {
            int thumb = th * th / g_content_h; if (thumb < S(30)) thumb = S(30);
            int pos = ty + (th - thumb) * g_scroll / maxs;
            rr_fill(G, n.right - S(13), pos, S(4), thumb, S(2), ARGB(60, 255, 255, 255));
        }
    }
    RECT s = L.server; int sw = s.right - s.left, sh = s.bottom - s.top;
    if (sh > S(70)) {
        glass(G, s, S(22));
        int on = g_online && g_state != ST_OFFLINE, conn = g_state == ST_CONNECTING;
        COLORREF pc = on ? C_GREEN : (conn ? C_DIM : C_RED);
        DWORD pa = on ? ARGB(255, 120, 226, 140) : conn ? ARGB(255, 161, 161, 170) : ARGB(255, 248, 113, 113);
        card_header(m, G, s, L"SERVER", pa);
        pill(m, G, s.right - S(20), s.top + S(13), on ? L"ONLINE" : (conn ? L"CONNECTING" : L"OFFLINE"), pc,
             (pa & 0x00FFFFFF) | (22u << 24), (pa & 0x00FFFFFF) | (80u << 24), pa);
        void *b; GdipCreateSolidFill(ARGB(18, 255, 255, 255), &b); GdipFillRectangleI(G, b, s.left + S(22), s.top + S(50), sw - S(44), 1); GdipDeleteBrush(b);
        int tw2 = (sw - S(44) - S(10)) / 2, ty = s.top + S(64), th2 = S(76);
        const wchar_t *lbl[2] = { L"LOBBIES OPEN", L"IN MATCHES" };
        int val[2] = { g_lobbies, g_players };
        for (int i = 0; i < 2; i++) {
            int tx = s.left + S(22) + i * (tw2 + S(10));
            rr_grad(G, tx, ty, tw2, th2, S(16), ARGB(12, 255, 255, 255), ARGB(5, 255, 255, 255));
            rr_line(G, tx, ty, tw2, th2, S(16), ARGB(22, 255, 255, 255));
            wchar_t v[16]; if (val[i] >= 0) swprintf(v, 16, L"%d", val[i]); else wcscpy(v, L"\x2014");
            text(m, f_big, RGB(255, 255, 255), tx + S(16), ty + S(10), tw2 - S(22), S(40), v, DT_SINGLELINE);
            text_sp(m, f_lbl, C_LBL, tx + S(16), ty + S(50), tw2 - S(20), S(18), lbl[i], DT_SINGLELINE, S(1));
        }
        EnterCriticalSection(&g_cs);
        int my = ty + th2 + S(16);
        text(m, f_small, C_DIM, s.left + S(24), my, sw - S(48), sh - (my - s.top) - S(14), g_status, DT_WORDBREAK);
        LeaveCriticalSection(&g_cs);
    }
}

static void toggle(void *G, RECT r, int on, int hot) {
    int w = r.right - r.left, h = r.bottom - r.top, k = h - S(8);
    if (on) { rr_fill(G, r.left, r.top, w, h, h / 2, ARGB(255, hot ? 255 : 240, hot ? 255 : 240, hot ? 255 : 240)); dot(G, r.right - S(4) - k, r.top + S(4), k, ARGB(255, 5, 5, 5)); }
    else { rr_fill(G, r.left, r.top, w, h, h / 2, ARGB(255, 30, 30, 34)); rr_line(G, r.left, r.top, w, h, h / 2, ARGB(hot ? 110 : 50, 255, 255, 255)); dot(G, r.left + S(4), r.top + S(4), k, ARGB(255, 161, 161, 170)); }
}
static void draw_settings(HDC m, void *G) {
    { void *b; GdipCreateSolidFill(ARGB(176, 0, 0, 0), &b); GdipFillRectangleI(G, b, 0, 0, L.W, L.H); GdipDeleteBrush(b); }      /* everything behind goes dark */
    RECT s = L.set; int sw = s.right - s.left, x = s.left + S(24);
    glass(G, s, S(22));
    card_header(m, G, s, L"SETTINGS", ARGB(255, 255, 255, 255));
    {   RECT r = L.sclose; int w = r.right - r.left, h = r.bottom - r.top, hot = g_hot == HOT_SCLOSE, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, k = S(4);
        rr_fill(G, r.left, r.top, w, h, h / 2, hot ? ARGB(255, 48, 48, 50) : ARGB(255, 16, 16, 17)); rr_line(G, r.left, r.top, w, h, h / 2, ARGB(hot ? 70 : 26, 255, 255, 255));
        void *pen; GdipCreatePen1(ARGB(hot ? 255 : 200, 255, 255, 255), (float)S(2) * 0.7f, 2, &pen);
        GdipDrawLineI(G, pen, cx - k, cy - k, cx + k, cy + k); GdipDrawLineI(G, pen, cx - k, cy + k, cx + k, cy - k); GdipDeletePen(pen); }
    { void *b; GdipCreateSolidFill(ARGB(18, 255, 255, 255), &b); GdipFillRectangleI(G, b, s.left + S(22), s.top + S(54), sw - S(44), 1); GdipDeleteBrush(b); }
    static const wchar_t *tt[2] = { L"Keep the launcher open while playing", L"Animations" };
    static const wchar_t *sub[2] = { L"Off: the launcher hides when the game starts and comes back when you close the game.",
                                     L"The opening, the closing and the moving background. Turn off for a still launcher." };
    int on[2] = { g_stay_open, g_anim_pref };
    for (int i = 0; i < 2; i++) { RECT t = L.tog[i]; int tw = t.left - S(18) - x;
        text(m, f_valb, RGB(255, 255, 255), x, t.top - S(4), tw, S(22), tt[i], DT_SINGLELINE | DT_END_ELLIPSIS);
        text(m, f_small, C_DIM, x, t.top + S(20), tw, S(36), sub[i], DT_WORDBREAK);
        toggle(G, t, on[i], g_hot == (i ? HOT_TOG2 : HOT_TOG1)); }
    {   RECT b = L.slink; int bw = b.right - b.left, bh = b.bottom - b.top, busy = g_link_state == 1 || g_link_state == 2,
            hot = !busy && g_hot == HOT_SLINK, tw = b.left - S(18) - x;
        text(m, f_valb, RGB(255, 255, 255), x, b.top - S(2), tw, S(22), L"Discord link (PUGs)", DT_SINGLELINE);
        const wchar_t *sub = g_link_state ? g_link_text : L"Get a code, then type /verify CODE in the PSO Discord to play PUGs.";
        COLORREF sc = g_link_state == 3 ? RGB(134, 239, 172) : g_link_state == 4 ? RGB(248, 113, 113) : C_DIM;   /* code green, problems red */
        text(m, f_small, sc, x, b.top + S(22), tw, S(36), sub, DT_WORDBREAK);
        rr_fill(G, b.left, b.top, bw, bh, bh / 2, ARGB(busy ? 8 : (hot ? 40 : 18), 255, 255, 255)); rr_line(G, b.left, b.top, bw, bh, bh / 2, ARGB(busy ? 24 : (hot ? 100 : 54), 255, 255, 255));
        text_sp(m, f_lbl, busy ? C_LBL : RGB(255, 255, 255), b.left + S(1), b.top, bw, bh, L"GET CODE",
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, S(1)); }
    {   RECT b = L.sfolder; int bw = b.right - b.left, bh = b.bottom - b.top, have = GetFileAttributesW(g_game) != INVALID_FILE_ATTRIBUTES, hot = have && g_hot == HOT_SFOLDER, tw = b.left - S(18) - x;
        text(m, f_valb, RGB(255, 255, 255), x, b.top - S(2), tw, S(22), L"Game folder", DT_SINGLELINE);
        text(m, f_small, C_DIM, x, b.top + S(22), tw, S(18), g_game, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        rr_fill(G, b.left, b.top, bw, bh, bh / 2, ARGB(have ? (hot ? 40 : 18) : 8, 255, 255, 255)); rr_line(G, b.left, b.top, bw, bh, bh / 2, ARGB(have ? (hot ? 100 : 54) : 24, 255, 255, 255));
        text_sp(m, f_lbl, have ? RGB(255, 255, 255) : C_LBL, b.left + S(1), b.top, bw, bh, L"OPEN", DT_CENTER | DT_VCENTER | DT_SINGLELINE, S(1));
        wchar_t foot[160]; int bn = PSO_BUILD;
        swprintf(foot, 160, g_installed_once ? L"Launcher build %d" : L"Launcher build %d  \x2022  You can change the game folder on the main screen before installing.", bn);
        text(m, f_small, C_LBL, x, s.bottom - S(34), sw - S(48), S(18), foot, DT_SINGLELINE | DT_END_ELLIPSIS); }
}

/* the banner, already scaled to its box (scaling the big picture on every frame would be too slow for the moving background) */
static void *g_hero_cache, *g_hc_src; static int g_hc_w, g_hc_h;
static void draw_hero(void *G) {
    RECT hr = L.hero; int hw = hr.right - hr.left, hh = hr.bottom - hr.top, rad = S(22);
    if (hw <= 0 || hh <= 0) return;
    void *clip = rr_path(hr.left, hr.top, hw, hh, rad); GdipSetClipPath(G, clip, 0);
    EnterCriticalSection(&g_imgcs);
    if (!g_hero_cache || g_hc_w != hw || g_hc_h != hh || g_hc_src != g_banner) {
        if (g_hero_cache) { GdipDisposeImage(g_hero_cache); g_hero_cache = NULL; }
        void *bmp = NULL, *g2 = NULL;
        if (g_banner && GdipCreateBitmapFromScan0(hw, hh, 0, 0x000E200B /* 32-bit, alpha ready */, NULL, &bmp) == 0 && bmp) {
            if (GdipGetImageGraphicsContext(bmp, &g2) == 0 && g2) { GdipSetInterpolationMode(g2, 7); GdipSetPixelOffsetMode(g2, 4); draw_img(g2, g_banner, 0, 0, hw, hh); GdipDeleteGraphics(g2); g_hero_cache = bmp; }
            else GdipDisposeImage(bmp); }
        g_hc_w = hw; g_hc_h = hh; g_hc_src = g_banner;
    }
    if (g_hero_cache) { GdipSetInterpolationMode(G, 5); GdipDrawImageRectI(G, g_hero_cache, hr.left, hr.top, hw, hh); GdipSetInterpolationMode(G, 7); }
    else draw_img(G, g_banner, hr.left, hr.top, hw, hh);
    LeaveCriticalSection(&g_imgcs);
    vgrad(G, hr.left, hr.bottom - S(70), hw, S(70), ARGB(0, 5, 5, 5), ARGB(120, 5, 5, 5));
    GdipResetClip(G); GdipDeletePath(clip);
    rr_line(G, hr.left, hr.top, hw, hh, rad, ARGB(30, 255, 255, 255));
}

static COLORREF fade_col(COLORREF c, float a) {                       /* a colour on the near-black background at strength a */
    int r = 5 + (int)((GetRValue(c) - 5) * a), g = 5 + (int)((GetGValue(c) - 5) * a), b = 5 + (int)((GetBValue(c) - 5) * a);
    return RGB(r, g, b);
}
static void solid(HDC m, int x, int y, int w, int h, COLORREF c) { RECT r = { x, y, x + w, y + h }; HBRUSH b = CreateSolidBrush(c); FillRect(m, &r, b); DeleteObject(b); }

/* Drawing every panel, picture and line of text afresh for each frame is what made the first version of this look
 * slow. Now the finished window (everything except what moves) is drawn once into a kept picture and drawn again
 * only when something in it changes (hover, state, progress, size). A frame is then: copy the kept picture, add the
 * dots that show between the panels, add the turning arrow. During the opening the panels are copied from the same
 * kept picture at their rising positions with a fade, which Windows does in one fast step each. */
static HDC g_ui_dc; static HBITMAP g_ui_bm; static HGDIOBJ g_ui_old; static int g_ui_w, g_ui_h, g_ui_dirty = 1, g_dirty_all = 1; static RECT g_dirty_rc;
static HBITMAP dib32(HDC ref, int w, int h, uint32_t **bits) {         /* a picture whose pixels can be reached directly */
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0 } }; void *p = NULL;
    HBITMAP bm = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &p, NULL, 0); if (bits) *bits = (uint32_t *)p; return bm;
}
static int hits(const RECT *a, const RECT *clip) { RECT t; return !clip || IntersectRect(&t, a, clip); }
static void render_static(HDC m, const RECT *clip) {                    /* everything that doesn't move; clip = only this part changed */
    int saved = SaveDC(m);
    if (clip) IntersectClipRect(m, clip->left, clip->top, clip->right, clip->bottom);
    SetBkMode(m, TRANSPARENT);
    if (clip) solid(m, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, RGB(5, 5, 5)); else solid(m, 0, 0, L.W, L.H, RGB(5, 5, 5));
    void *G; GdipCreateFromHDC(m, &G);
    GdipSetSmoothingMode(G, 4); GdipSetPixelOffsetMode(G, 4); GdipSetInterpolationMode(G, 7);
    RECT top = { 0, 0, L.W, L.tb }, cards; UnionRect(&cards, &L.notes, &L.server);
    if (hits(&top, clip)) { draw_title(m, G); draw_window_buttons(G); }
    if (hits(&L.hero, clip)) draw_hero(G);
    if (hits(&L.act, clip)) draw_action(m, G);
    if (hits(&cards, clip)) draw_cards(m, G);
    if (g_settings) draw_settings(m, G);
    GdipDeleteGraphics(G);
    RestoreDC(m, saved);
}
static void ui_ensure(HDC ref) {
    if (!g_ui_dc || g_ui_w != L.W || g_ui_h != L.H) {
        if (g_ui_dc) { SelectObject(g_ui_dc, g_ui_old); DeleteObject(g_ui_bm); DeleteDC(g_ui_dc); }
        g_ui_dc = CreateCompatibleDC(ref); g_ui_bm = dib32(ref, L.W > 0 ? L.W : 1, L.H > 0 ? L.H : 1, NULL);
        g_ui_old = SelectObject(g_ui_dc, g_ui_bm); g_ui_w = L.W; g_ui_h = L.H; g_ui_dirty = g_dirty_all = 1;
    }
    if (g_ui_dirty) { int all = g_dirty_all; RECT rc = g_dirty_rc; g_ui_dirty = g_dirty_all = 0; SetRectEmpty(&g_dirty_rc); render_static(g_ui_dc, all ? NULL : &rc); }
}
static void compose(HDC m, uint32_t *px) {                              /* one frame into m (px = m's pixels) */
    ui_ensure(m);
    double t = intro_t(); int intro = g_intro_on; void *G;
    RECT tp = L.title; tp.right = tp.left + g_title_w;
    RECT panels[8] = { L.hero, L.act, L.notes, L.server, tp, L.bmin, L.bclose, L.gear };
    SetBkMode(m, TRANSPARENT);
    if (g_outro_on) {                                                    /* closing: the reverse of the opening, quick. Cards first, title last */
        double ot = outro_t();
        solid(m, 0, 0, L.W, L.H, RGB(5, 5, 5)); GdiFlush();
        if (!g_settings) dots_px(px, L.W, L.H, 1.0f - seg(ot, 0.0, 0.30), NULL, 0);
        for (int part = 0; part < 4; part++) {
            static const double start[4] = { 0.10, 0.07, 0.035, 0.0 };
            double p = (ot - start[part]) / 0.27; if (p < 0) p = 0; if (p > 1) p = 1;
            float e = 1.0f - (float)(p * p * p); if (e <= 0.004f) continue;
            int dy = (int)((1 - e) * (float)S(26)); RECT c;
            if (part == 0) SetRect(&c, 0, 0, L.W, L.tb - S(6)); else if (part == 1) c = L.hero; else if (part == 2) c = L.act; else UnionRect(&c, &L.notes, &L.server);
            if (g_settings) SetRect(&c, 0, part * L.H / 4, L.W, (part + 1) * L.H / 4);      /* with the settings card open the window goes in four bands */
            else if (part) InflateRect(&c, S(2), S(2));
            if (c.left < 0) c.left = 0; if (c.top < 0) c.top = 0; if (c.right > L.W) c.right = L.W; if (c.bottom > L.H) c.bottom = L.H;
            int cw = c.right - c.left, vis = c.bottom - c.top; if (c.top + dy + vis > L.H) vis = L.H - c.top - dy; if (cw <= 0 || vis <= 0) continue;
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(e * 255.0f), 0 };
            GdiAlphaBlend(m, c.left, c.top + dy, cw, vis, g_ui_dc, c.left, c.top, cw, vis, bf);
        }
        return;
    }
    if (!intro) {
        BitBlt(m, 0, 0, L.W, L.H, g_ui_dc, 0, 0, SRCCOPY); GdiFlush();
        if (!g_settings) dots_px(px, L.W, L.H, 1.0f, panels, 8);
        if (spinning() && g_arrow_on && !g_settings) { GdipCreateFromHDC(m, &G); GdipSetSmoothingMode(G, 4); GdipSetPixelOffsetMode(G, 4);
            draw_update_arrow(G, g_arrow_cx, g_arrow_cy, (float)S(7), g_spin, g_arrow_col); GdipDeleteGraphics(G); }
        return;
    }
    /* the opening */
    float in = seg(t, 0.10, 0.60), fly = seg(t, 1.18, 0.62), name = seg(t, 0.48, 0.55), out = seg(t, 1.08, 0.36), line = seg(t, 0.58, 0.62);
    RECT tl; title_logo_rect(&tl);
    float big = (float)S(112) * (0.84f + 0.16f * in), small = (float)(tl.right - tl.left), sz = big + (small - big) * fly;
    float cx0 = L.W / 2.0f, cy0 = L.H / 2.0f - (float)S(34), cx1 = (tl.left + tl.right) / 2.0f, cy1 = (tl.top + tl.bottom) / 2.0f;
    float cx = cx0 + (cx1 - cx0) * fly, cy = cy0 + (cy1 - cy0) * fly;
    solid(m, 0, 0, L.W, L.H, RGB(5, 5, 5)); GdiFlush();
    dots_px(px, L.W, L.H, seg(t, 0.0, 0.7), NULL, 0);
    if (t < 1.86) glow_px(px, L.W, L.H, cx, cy, sz * 0.5f + (float)S(120) * (1 - fly), 46.0f * in * (1 - fly));      /* the soft light behind the logo */
    int arrow_ok = 0;
    for (int part = 0; part < 4; part++) {                               /* each part rises into place and comes out of the dark, one after the other */
        static const double start[4] = { 1.30, 1.40, 1.52, 1.64 };
        float e = seg(t, start[part], 0.74); if (e <= 0.004f) continue;
        int dy = (int)((1 - e) * (float)S(34)); RECT c;
        if (part == 0) SetRect(&c, 0, 0, L.W, L.tb - S(6)); else if (part == 1) c = L.hero; else if (part == 2) c = L.act; else UnionRect(&c, &L.notes, &L.server);
        if (part) InflateRect(&c, S(2), S(2));
        if (c.left < 0) c.left = 0; if (c.top < 0) c.top = 0; if (c.right > L.W) c.right = L.W; if (c.bottom > L.H) c.bottom = L.H;
        int cw = c.right - c.left, ch = c.bottom - c.top; if (cw <= 0 || ch <= 0) continue;
        int vis = ch; if (c.top + dy + vis > L.H) vis = L.H - c.top - dy; if (vis <= 0) continue;
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(e * 255.0f), 0 };
        GdiAlphaBlend(m, c.left, c.top + dy, cw, vis, g_ui_dc, c.left, c.top, cw, vis, bf);
        if (part == 0 && t < 1.80) solid(m, tl.left - 1, tl.top - 1 + dy, tl.right - tl.left + 2, tl.bottom - tl.top + 2, RGB(9, 9, 10));      /* the logo is still on its way to this spot */
        if (part == 2 && e > 0.985f) arrow_ok = 1;
    }
    GdipCreateFromHDC(m, &G); GdipSetSmoothingMode(G, 4); GdipSetPixelOffsetMode(G, 4); GdipSetInterpolationMode(G, 7);
    if (arrow_ok && spinning() && g_arrow_on) draw_update_arrow(G, g_arrow_cx, g_arrow_cy, (float)S(7), g_spin, g_arrow_col);
    if (t < 1.86) {                                                      /* the logo arrives in the middle with the name under it, then flies to its place in the title */
        int lx = (int)(cx - sz / 2), ly = (int)(cy - sz / 2), ls = (int)sz;
        EnterCriticalSection(&g_imgcs);
        if (g_logo && in > 0.01f) GdipDrawImageRectRectI(G, g_logo, lx, ly, ls, ls, 0, 0, 128, 128, 2, NULL, NULL, NULL);
        LeaveCriticalSection(&g_imgcs);
        if (in < 0.996f) { void *b; GdipCreateSolidFill(ARGB((int)((1 - in) * 255), 5, 5, 5), &b); GdipFillRectangleI(G, b, lx - 2, ly - 2, ls + 4, ls + 4); GdipDeleteBrush(b); }
        float na = name * (1 - out);
        if (na > 0.01f) {
            int ny = (int)(cy0 + (float)S(112) / 2 + (float)S(30) + (1 - name) * (float)S(12)), sp1 = S(4), sp2 = S(7);
            int w1 = text_w_sp(m, f_splash, L"PSO UE4", sp1), w2 = text_w_sp(m, f_splash2, L"LAUNCHER", sp2);
            text_sp(m, f_splash, fade_col(RGB(255, 255, 255), na), (L.W - w1) / 2, ny, w1 + S(12), S(42), L"PSO UE4", DT_SINGLELINE, sp1);
            text_sp(m, f_splash2, fade_col(C_DIM, na), (L.W - w2) / 2, ny + S(46), w2 + S(14), S(20), L"LAUNCHER", DT_SINGLELINE, sp2);
            int lw = (int)((float)S(210) * line), la = (int)(150 * (1 - out)), lyy = ny + S(78);
            if (lw > 2 && la > 0) { hgrad(G, L.W / 2 - lw / 2, lyy, lw / 2, 1, ARGB(0, 255, 255, 255), ARGB(la, 255, 255, 255)); hgrad(G, L.W / 2, lyy, lw / 2, 1, ARGB(la, 255, 255, 255), ARGB(0, 255, 255, 255)); }
        }
    }
    GdipDeleteGraphics(G);
    if (t >= INTRO_END) g_intro_on = 0;
}
static HDC g_bb_dc; static HBITMAP g_bb_bm; static HGDIOBJ g_bb_old; static int g_bb_w, g_bb_h; static uint32_t *g_bb_px;
static void draw_to(HDC dc) {
    if (L.W <= 0 || L.H <= 0) return;
    if (!g_bb_dc || g_bb_w != L.W || g_bb_h != L.H) {
        if (g_bb_dc) { SelectObject(g_bb_dc, g_bb_old); DeleteObject(g_bb_bm); DeleteDC(g_bb_dc); }
        g_bb_dc = CreateCompatibleDC(dc); g_bb_bm = dib32(dc, L.W, L.H, &g_bb_px); g_bb_old = SelectObject(g_bb_dc, g_bb_bm); g_bb_w = L.W; g_bb_h = L.H;
    }
    compose(g_bb_dc, g_bb_px);
    BitBlt(dc, 0, 0, L.W, L.H, g_bb_dc, 0, 0, SRCCOPY);
}
static void paint(HWND h) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); draw_to(dc); EndPaint(h, &ps); }
/* Is anything moving? The opening always; the dots only while this window is the one in use and the game isn't running. */
static int anim_active(HWND h) { return g_intro_on || g_outro_on || (g_motion && g_state != ST_RUNNING && IsWindowVisible(h) && !IsIconic(h) && GetForegroundWindow() == h); }
/* One frame of movement. Called once per screen refresh from the main loop (see wWinMain), so the opening and the dots
 * run at the monitor's own rate (60, 144, 240 ...) with even spacing, instead of at the uneven pace of a Windows timer. */
static double g_frame_last;
static void frame(HWND h) {
    double now = now_s(), dt = g_frame_last > 0 ? now - g_frame_last : 1.0 / 60; g_frame_last = now;
    if (dt > 0.05) dt = 0.05; if (dt < 0) dt = 0;
    dots_step(dt); g_spin = (float)fmod(now * 300.0, 360.0);
    HDC dc = GetDC(h); draw_to(dc); ReleaseDC(h, dc); ValidateRect(h, NULL);
    if (g_outro_on) {                                                   /* closing: the whole window fades with it, then it is gone */
        double ot = outro_t(), a = 1.0 - (ot - 0.16) / 0.27; if (a > 1) a = 1; if (a < 0) a = 0;
        SetLayeredWindowAttributes(h, 0, (BYTE)(a * 255.0), LWA_ALPHA);
        if (ot >= OUTRO_END) DestroyWindow(h);
    }
}
/* ------------------------------------------------------------ window */
static void touch(HWND h) { g_ui_dirty = g_dirty_all = 1; InvalidateRect(h, NULL, FALSE); }      /* the kept picture must be drawn again */
static void touch_rc(HWND h, const RECT *rc) {                          /* ... only this part of it */
    if (!g_dirty_all) { RECT u; UnionRect(&u, &g_dirty_rc, rc); g_dirty_rc = u; }
    g_ui_dirty = 1; InvalidateRect(h, rc, FALSE);
}
static void touch_item(HWND h, int item) {
    RECT top = { 0, 0, L.W, L.tb };
    if (item == HOT_PLAY || item == HOT_CHANGE) touch_rc(h, &L.act);
    else if (item == HOT_MIN || item == HOT_CLOSE || item == HOT_GEAR) touch_rc(h, &top);
    else if (item >= HOT_SCLOSE && item <= HOT_SLINK) touch_rc(h, &L.set);
}
static int hit_item(int x, int y) {
    if (g_settings) {                                                    /* the card is on top: only its own controls count */
        if (in_rect(&L.sclose, x, y)) return HOT_SCLOSE;
        if (in_rect(&L.tog[0], x, y)) return HOT_TOG1;
        if (in_rect(&L.tog[1], x, y)) return HOT_TOG2;
        if (in_rect(&L.sfolder, x, y)) return HOT_SFOLDER;
        if (in_rect(&L.slink, x, y)) return HOT_SLINK;
        return in_rect(&L.set, x, y) ? HOT_NONE : HOT_SOUT;
    }
    if (in_rect(&L.bclose, x, y)) return HOT_CLOSE;
    if (in_rect(&L.bmin, x, y)) return HOT_MIN;
    if (in_rect(&L.gear, x, y)) return HOT_GEAR;
    if (in_rect(&L.play, x, y)) return HOT_PLAY;
    if (loc_shown() && g_state == ST_NEED && in_rect(&L.change, x, y)) return HOT_CHANGE;
    return HOT_NONE;
}

/* ---- the player's choices: one small text file */
static void settings_file(wchar_t *p) { swprintf(p, MAX_PATH * 2, L"%ls\\launcher-settings.txt", g_base); }
static void save_settings(void) {
    wchar_t p[MAX_PATH * 2]; char dir[MAX_PATH * 4] = ""; settings_file(p);
    WideCharToMultiByte(CP_UTF8, 0, g_game, -1, dir, sizeof dir, NULL, NULL);
    FILE *f = _wfopen(p, L"wb"); if (!f) return;
    fprintf(f, "gamedir=%s\r\nstayopen=%d\r\nanimations=%d\r\n", dir, g_stay_open, g_anim_pref); fclose(f);
}
static void load_settings(void) {
    wchar_t p[MAX_PATH * 2]; char line[MAX_PATH * 4]; settings_file(p);
    FILE *f = _wfopen(p, L"rb"); if (!f) return;
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "gamedir=", 8) && line[8]) { wchar_t w[MAX_PATH]; if (MultiByteToWideChar(CP_UTF8, 0, line + 8, -1, w, MAX_PATH) > 3 && w[1] == L':') wcscpy(g_game, w); }
        else if (!strncmp(line, "stayopen=", 9)) g_stay_open = line[9] == '1';
        else if (!strncmp(line, "animations=", 11)) g_anim_pref = line[11] != '0'; }
    fclose(f);
}

/* ---- choosing where the game is installed (only before the first install) */
#ifdef PSO_SHOT
static const wchar_t *g_test_pick;                                       /* test build only: the folder "chosen", no window */
#endif
/* ---- Discord link for PUGs
 * The launcher asks the server for a short code for the Steam account signed in on this PC; the player types
 * /verify CODE in Discord. The server only hands out the code when the launcher sends the key the game add-on saved on this
 * PC after that account's game signed in (or, without it, from the same connection), so nobody can get a code for
 * someone else's account. */
static void link_set(int st, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt); _vsnwprintf(g_link_text, 299, fmt, ap); va_end(ap); g_link_text[299] = 0;
    g_link_state = st;
#ifdef PSO_SHOT
    wprintf(L"LINK %d: %ls\n", st, g_link_text); fflush(stdout);
#endif
    if (g_wnd) PostMessage(g_wnd, WM_REFRESH, 0, 0);
}

static DWORD WINAPI link_thread(LPVOID u) {
    (void)u;
    for (int i = 0; i < 50 && !g_steam.ok; i++) Sleep(100);           /* Steam info is read when the launcher opens */
    if (!g_steam.ok || !g_steam.sid) { link_set(4, L"No Steam account found on this PC. Start Steam and sign in, then try again."); return 0; }
    link_set(2, L"Getting your code...");
    /* the key the game add-on saved on this PC after the game signed in: it proves this is the same PC even when the game
       goes through ExitLag or a VPN and the launcher doesn't (only sent for the same Steam account) */
    char sec[72] = "";
    {   wchar_t file[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", file, MAX_PATH - 40);
        if (n && n < MAX_PATH - 40) { wcscat(file, L"\\UE4PSO\\pso-link.txt");
            HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) { char b[160] = ""; DWORD got = 0; ReadFile(h, b, sizeof b - 1, &got, NULL); CloseHandle(h); b[got] = 0;
                char want[24]; snprintf(want, sizeof want, "%llu\t", g_steam.sid); size_t wl = strlen(want);
                if (!strncmp(b, want, wl)) { char *p = b + wl; size_t k = 0;
                    while (k < sizeof sec - 1 && ((p[k] >= '0' && p[k] <= '9') || (p[k] >= 'a' && p[k] <= 'f'))) { sec[k] = p[k]; k++; } sec[k] = 0; } } } }
    char js[200];
    if (sec[0]) snprintf(js, sizeof js, "{\"steamid\":\"%llu\",\"secret\":\"%s\"}", g_steam.sid, sec);
    else snprintf(js, sizeof js, "{\"steamid\":\"%llu\"}", g_steam.sid);
    DWORD code = 0;
    char *ans = http_post_json(L"/link/code", js, &code);
    if (!ans) { link_set(4, L"Could not reach the server. Check your internet and press GET CODE again."); return 0; }
    if (code == 404 || code == 405) { free(ans); link_set(4, L"The server doesn't have Discord linking yet (it needs its update)."); return 0; }
    wchar_t *c6 = json_wstr(ans, "code"), *msg = json_wstr(ans, "message"), *err = json_wstr(ans, "error"), *cool = json_wstr(ans, "cooldown");
    if (c6 && *c6 && code == 200) {
        wcsncpy(g_link_code, c6, 15); g_link_code[15] = 0;
        wchar_t cmd[40]; swprintf(cmd, 40, L"%ls", g_link_code);
        int copied = 0;
        if (OpenClipboard(g_wnd)) {                        /* just the code: Discord needs /verify typed so its command box opens */
            size_t bytes = (wcslen(cmd) + 1) * sizeof(wchar_t); HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hg) { memcpy(GlobalLock(hg), cmd, bytes); GlobalUnlock(hg); EmptyClipboard(); copied = SetClipboardData(CF_UNICODETEXT, hg) != NULL; }
            CloseClipboard();
        }
        if (cool && *cool)
            link_set(3, L"Your code: %ls%ls  \x2022  Type /verify in the PSO Discord, then paste it. %ls", g_link_code,
                     copied ? L" (copied)" : L"", cool);
        else
            link_set(3, L"Your code: %ls%ls  \x2022  In the PSO Discord type /verify, then paste the code (within 10 minutes).", g_link_code,
                     copied ? L" (copied)" : L"");
    } else if (msg && *msg) link_set(4, L"%ls", msg);
    else link_set(4, L"%ls", err && *err ? err : L"The server could not make a code. Try again later.");
    free(c6); free(msg); free(err); free(cool); free(ans);
    return 0;
}
static void link_start(HWND h) {
    (void)h;
    if (g_link_state == 1 || g_link_state == 2) return;
    g_link_state = 2;
    CloseHandle(CreateThread(NULL, 0, link_thread, NULL, 0, NULL));
}

static int pick_folder(HWND owner, wchar_t *out) {                       /* out: MAX_PATH characters */
    int ok = 0; IFileOpenDialog *d = NULL;
#ifdef PSO_SHOT
    if (g_test_pick) { wcscpy(out, g_test_pick); return 1; }
#endif
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d)) && d) {
        DWORD o = 0; d->lpVtbl->GetOptions(d, &o); d->lpVtbl->SetOptions(d, o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        d->lpVtbl->SetTitle(d, L"Where should the game be installed?");
        if (SUCCEEDED(d->lpVtbl->Show(d, owner))) { IShellItem *it = NULL;
            if (SUCCEEDED(d->lpVtbl->GetResult(d, &it)) && it) { PWSTR w = NULL;
                if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &w)) && w) { if (wcslen(w) < MAX_PATH) { wcscpy(out, w); ok = 1; } CoTaskMemFree(w); }
                it->lpVtbl->Release(it); } }
        d->lpVtbl->Release(d); return ok;
    }
    BROWSEINFOW bi; memset(&bi, 0, sizeof bi); bi.hwndOwner = owner; bi.lpszTitle = L"Where should the game be installed?"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi); if (id) { ok = SHGetPathFromIDListW(id, out) != 0; CoTaskMemFree(id); }
    return ok;
}
static int dir_empty(const wchar_t *dir) {
    wchar_t q[MAX_PATH + 4]; WIN32_FIND_DATAW fd; int empty = 1; swprintf(q, MAX_PATH + 4, L"%ls\\*", dir);
    HANDLE h = FindFirstFileW(q, &fd); if (h == INVALID_HANDLE_VALUE) return 1;
    do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { empty = 0; break; } } while (FindNextFileW(h, &fd));
    FindClose(h); return empty;
}
static void choose_install_dir(HWND h) {
    if (g_installed_once || g_state != ST_NEED) return;                  /* only while the launcher is waiting at "Install" */
    wchar_t dir[MAX_PATH] = L"", fin[MAX_PATH * 2], test[MAX_PATH * 2], msg[900];
    g_ui_busy = 1; int picked = pick_folder(h, dir); g_ui_busy = 0;
    if (!picked || g_updating) return;
    size_t n = wcslen(dir); while (n > 3 && dir[n - 1] == L'\\') dir[--n] = 0;
    if (n < 3 || dir[1] != L':') { MessageBoxW(h, L"Please pick a folder on one of this PC's drives (C:, D: ...), not a network place.", L"PSO UE4 Launcher", MB_ICONINFORMATION); return; }
    /* an empty folder, or one that already is a PSO folder, is used as it is; anything else gets a "PSO UE4" folder inside it,
       so the game's files never end up mixed with other things */
    const wchar_t *leaf = wcsrchr(dir, L'\\'); leaf = leaf ? leaf + 1 : dir;
    swprintf(test, MAX_PATH * 2, L"%ls\\ProSoccerOnline.exe", dir);
    if (n > 3 && (dir_empty(dir) || !_wcsicmp(leaf, L"PSO UE4") || GetFileAttributesW(test) != INVALID_FILE_ATTRIBUTES)) wcscpy(fin, dir);
    else swprintf(fin, MAX_PATH * 2, n == 3 ? L"%lsPSO UE4" : L"%ls\\PSO UE4", dir);
    if (wcslen(fin) > 150) { MessageBoxW(h, L"That folder's path is too long for the game. Please pick a place with a shorter path.", L"PSO UE4 Launcher", MB_ICONINFORMATION); return; }
    swprintf(test, MAX_PATH * 2, L"%ls\\write-test.tmp", fin); make_dirs(test);
    HANDLE f = CreateFileW(test, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE) { swprintf(msg, 900, L"Windows doesn't let the launcher save files in\n%ls\n\nPick another folder (for example one inside Documents, or on another drive).", fin); MessageBoxW(h, msg, L"PSO UE4 Launcher", MB_ICONWARNING); return; }
    CloseHandle(f);
    wchar_t root[4] = { fin[0], L':', L'\\', 0 }; ULARGE_INTEGER fr; LONGLONG need = g_total > g_done ? g_total - g_done : 0; if (need < (5LL << 30)) need = 5LL << 30;
    if (GetDiskFreeSpaceExW(root, &fr, NULL, NULL) && (LONGLONG)fr.QuadPart < need + (512LL << 20)) {
        swprintf(msg, 900, L"Drive %lc: has %.1f GB free and the game needs about %.1f GB.\n\nUse this folder anyway?", fin[0], fr.QuadPart / 1073741824.0, need / 1073741824.0);
        if (MessageBoxW(h, msg, L"PSO UE4 Launcher", MB_YESNO | MB_ICONWARNING) != IDYES) return; }
    if (g_state != ST_NEED) return;                                      /* (something changed while the question was open) */
    wcsncpy(g_game, fin, MAX_PATH - 1); g_game[MAX_PATH - 1] = 0; save_settings();
    g_reloc = 1; g_go = 1;                                               /* the worker looks at this folder's files now */
    touch(h);
}
static void set_hot(HWND h, int hot) {
    if (hot == g_hot) return;
    int was = g_hot; g_hot = hot; touch_item(h, was); touch_item(h, hot);
    TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
}
static int g_paint_pending, g_tracking, g_full_refresh = 1, g_last_state = -1, g_modal;
static void request_paint(HWND h) {
    if (!g_paint_pending) { g_paint_pending = 1; SetTimer(h, 1, g_state == ST_DOWNLOADING ? 100 : 33, NULL); }      /* download progress: ten times a second is plenty */
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        calc_layout(h); dots_setup();
        SetTimer(h, 4, 16, NULL);                                                     /* keeps things moving while the window is dragged */
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
        if (g_settings || g_outro_on) return HTCLIENT;
        if (p.y < L.tb && hit_item(p.x, p.y) == HOT_NONE) return HTCAPTION;
        if (in_rect(&L.hero, p.x, p.y)) return HTCAPTION;               /* drag by the banner too */
        return HTCLIENT;
    }
    case WM_SIZE: calc_layout(h); dots_setup(); touch(h); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = S(900); ((MINMAXINFO *)l)->ptMinTrackSize.y = S(560); return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w); make_fonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        calc_layout(h); dots_setup(); touch(h);
        return 0;
    }
    case WM_ENTERSIZEMOVE: g_modal = 1; break;
    case WM_EXITSIZEMOVE: g_modal = 0; g_frame_last = 0; break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(h); return 0;
    case WM_MOUSEMOVE:
        g_mouse.x = GET_X_LPARAM(l); g_mouse.y = GET_Y_LPARAM(l); g_mouse_in = 1;
        if (!g_tracking) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t); g_tracking = 1; }
        set_hot(h, hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l))); return 0;
    case WM_MOUSELEAVE: g_mouse_in = 0; g_tracking = 0; if (g_hot != HOT_NONE) { int was = g_hot; g_hot = HOT_NONE; touch_item(h, was); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && ((g_hot == HOT_PLAY && btn_enabled()) || g_hot == HOT_CHANGE || g_hot == HOT_GEAR || g_hot == HOT_SCLOSE || g_hot == HOT_TOG1 || g_hot == HOT_TOG2 ||
                                      (g_hot == HOT_SLINK && g_link_state != 1 && g_link_state != 2) ||
                                      (g_hot == HOT_SFOLDER && GetFileAttributesW(g_game) != INVALID_FILE_ATTRIBUTES))) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (g_intro_on) { g_intro_on = 0; InvalidateRect(h, NULL, FALSE); return 0; }      /* a click skips the opening */
        g_pressed = hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l)); if (g_pressed) { SetCapture(h); touch_item(h, g_pressed); } return 0;
    case WM_LBUTTONUP: {
        int it = hit_item(GET_X_LPARAM(l), GET_Y_LPARAM(l)), pr = g_pressed;
        g_pressed = HOT_NONE; ReleaseCapture(); touch_item(h, pr);
        if (it != pr) return 0;
        if (it == HOT_CLOSE) PostMessage(h, WM_CLOSE, 0, 0);
        else if (it == HOT_MIN) ShowWindow(h, SW_MINIMIZE);
        else if (it == HOT_GEAR) { g_settings = 1; g_hot = HOT_NONE; touch(h); }
        else if (it == HOT_SCLOSE || it == HOT_SOUT) { g_settings = 0; g_hot = HOT_NONE; touch(h); }
        else if (it == HOT_TOG1) { g_stay_open = !g_stay_open; save_settings(); touch_rc(h, &L.set); }
        else if (it == HOT_TOG2) { g_anim_pref = !g_anim_pref; g_motion = g_sys_motion && g_anim_pref; save_settings(); touch(h); }
        else if (it == HOT_SLINK) link_start(h);
        else if (it == HOT_SFOLDER) { if (GetFileAttributesW(g_game) != INVALID_FILE_ATTRIBUTES) ShellExecuteW(h, L"open", g_game, NULL, NULL, SW_SHOWNORMAL); }
        else if (it == HOT_CHANGE && !g_updating) choose_install_dir(h);
        else if (it == HOT_PLAY && btn_enabled()) { if (g_state == ST_READY) play(); else g_go = 1; }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p);
        if (in_rect(&L.notes, p.x, p.y)) { g_scroll -= GET_WHEEL_DELTA_WPARAM(w) * S(60) / WHEEL_DELTA; touch_rc(h, &L.notes); }
        return 0;
    }
    case WM_KEYDOWN:
        if (g_intro_on) { g_intro_on = 0; InvalidateRect(h, NULL, FALSE); return 0; }      /* so does a key */
        if (g_settings) { if (w == VK_ESCAPE) { g_settings = 0; touch(h); } return 0; }
        if ((w == VK_RETURN || w == VK_SPACE) && btn_enabled()) { if (g_state == ST_READY) play(); else g_go = 1; }
        return 0;
    case WM_TIMER:
        if (w == 1) { KillTimer(h, 1); g_paint_pending = 0;
            if (g_state == ST_DOWNLOADING && !g_full_refresh) { touch_rc(h, &L.act); touch_rc(h, &L.server); }      /* download progress only changes these two */
            else { g_full_refresh = 0; touch(h); } }
        else if (w == 3) {
            if (spinning() && (!anim_active(h) || g_modal)) { g_spin = (float)fmod(now_s() * 300.0, 360.0); RECT pr = L.play; InflateRect(&pr, S(12), S(12)); InvalidateRect(h, &pr, FALSE); }
        }
        else if (w == 5) DestroyWindow(h);
        else if (w == 4) {
            if (g_modal && anim_active(h)) frame(h);       /* the main loop isn't running while the window is dragged or resized */
        }
        else if (w == 2) {      /* download speed, smoothed */
            LONGLONG d = g_done - g_last_done; g_last_done = g_done;
            if (g_state == ST_DOWNLOADING && d >= 0) g_speed = g_speed ? g_speed * 0.7 + d * 0.3 : (double)d;
            else g_speed = 0;
        }
        return 0;
    case WM_REFRESH:
        if (w == 1) g_scroll = 0;
        if (!(w == 2 || (w == 3 && g_state == ST_DOWNLOADING && g_last_state == ST_DOWNLOADING))) g_full_refresh = 1;      /* anything but download progress and its status line */
        g_last_state = g_state;
        request_paint(h);
        return 0;
    case WM_APP + 0x77: ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); return 0;
    case WM_PLAY_NOW: if (g_state == ST_READY) play_now(); return 0;     /* build 24: the update check after Play found nothing new */
    case WM_APP + 0x78: g_settings = 0; PostMessage(h, WM_CLOSE, 0, 0); return 0;       /* the new launcher has been started: this one leaves */
    case WM_CLOSE:
        /* closing: a short goodbye first (not when hidden / minimised, not with animations off, and a second close is immediate) */
        if (!g_outro_on && g_motion && IsWindowVisible(h) && !IsIconic(h)) {
            g_outro_on = 1; g_intro_on = 0; g_outro0 = now_s();
            SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED); SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
            SetTimer(h, 5, 900, NULL);                                    /* whatever happens, the window is gone within a second */
            return 0;
        }
        DestroyWindow(h); return 0;
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

/* ------------------------------------------------------------ the launcher keeps itself up to date
 * In the background it fetches the launcher the server hands out (/download, the file the owner uploads in the owner panel).
 * When that one has a higher build number, and nothing is going on (no download, no game, no dialog), it is put in place of
 * this file - a running .exe can be renamed on Windows - and started; this one closes. */
static int su_build(const unsigned char *b, size_t n) {                  /* the number after the first build tag; 0 = none */
    /* the key is read from our own tag through a pointer the compiler can't see through, so the text exists only once in this
       program (a second copy in front of the real tag would make the add-on read "build 0") */
    const size_t kl = 18; const char *volatile keyp = g_build_tag; const char *key = keyp;
    for (size_t i = 0; i + kl + 1 < n; i++)
        if (b[i] == key[0] && !memcmp(b + i, key, kl)) { int v = 0; for (size_t j = i + kl; j < n && b[j] >= '0' && b[j] <= '9' && v < 100000; j++) v = v * 10 + (b[j] - '0'); return v; }
    return 0;
}
static int su_valid(const unsigned char *b, size_t n) {                  /* a whole 64-bit Windows program that is our launcher */
    static const wchar_t cls[] = L"UE4PSOLauncher";
    if (n < 300000 || n > 20000000 || b[0] != 'M' || b[1] != 'Z') return 0;
    DWORD pe; memcpy(&pe, b + 0x3C, 4);
    if (pe > n - 0x200 || memcmp(b + pe, "PE\0\0", 4)) return 0;
    WORD mach, ns, opt; memcpy(&mach, b + pe + 4, 2); memcpy(&ns, b + pe + 6, 2); memcpy(&opt, b + pe + 20, 2);
    size_t sec = (size_t)pe + 24 + opt;
    if (mach != 0x8664 || !ns || ns > 64 || sec + (size_t)ns * 40 > n) return 0;
    for (int i = 0; i < ns; i++) { DWORD rs, rp; memcpy(&rs, b + sec + i * 40 + 16, 4); memcpy(&rp, b + sec + i * 40 + 20, 4);
        if (rp && (size_t)rp + rs > n) return 0; }                       /* a cut-off file ends before its last part */
    size_t kl = sizeof cls - sizeof(wchar_t); int found = 0;
    for (size_t i = 0; i + kl < n && !found; i++) if (b[i] == 'U' && !memcmp(b + i, cls, kl)) found = 1;
    return found;
}
static unsigned char *http_get_bin(const wchar_t *path, size_t *n) {     /* the whole file, or nothing */
    HINTERNET r = open_req(path, 0); if (!r) return NULL;
    DWORD cl = 0, sz = sizeof cl; unsigned char *buf = NULL; size_t len = 0, cap = 0; int ok = 0;
    if (status_code(r) == 200 && WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, NULL, &cl, &sz, NULL) && cl >= 300000 && cl <= 20000000
        && (buf = malloc((cap = (size_t)cl + 16)))) {
        for (;;) { DWORD got = 0, want = (DWORD)(cap - len > 65536 ? 65536 : cap - len);
            if (!want || !WinHttpReadData(r, buf + len, want, &got) || !got) break; len += got; }
        ok = len == cl;                                                  /* every byte the server announced */
    }
    WinHttpCloseHandle(r);
    if (!ok) { free(buf); return NULL; }
    *n = len; return buf;
}
static int su_quiet(void) {                                              /* nothing is going on that a restart would break */
    int st = g_state;
    return (st == ST_READY || st == ST_NEED || st == ST_OFFLINE || st == ST_ERROR || st == ST_WAITING) && !g_ui_busy && !g_outro_on && !g_monitoring && !game_running();
}
static DWORD WINAPI selfupd_thread(LPVOID u) {
    (void)u;
    wchar_t exe[MAX_PATH], nw[MAX_PATH + 8], old[MAX_PATH + 8];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
    swprintf(nw, MAX_PATH + 8, L"%ls.new", exe); swprintf(old, MAX_PATH + 8, L"%ls.old", exe);
    for (int i = 0; i < 12 && GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES; i++) { if (DeleteFileW(old)) break; Sleep(500); }   /* the launcher we replaced may still be closing */
    DeleteFileW(nw);
    for (int round = 0;; round++) {
        for (int t = 0; !g_online && t < 600; t++) Sleep(200);
        LONGLONG want_s; char want_h[65];
        EnterCriticalSection(&g_selfcs); want_s = g_self_s; strcpy(want_h, g_self_h2); LeaveCriticalSection(&g_selfcs);
        size_t n = 0; unsigned char *b = g_online && want_s > 0 ? http_get_bin(L"/download", &n) : NULL;
        if (b) {                                                         /* build 26: exactly the file the owner signed, or nothing */
            unsigned char d[32]; char hx[65];
            if ((LONGLONG)n != want_s || !sha256_buf(b, n, d)) { free(b); b = NULL; }
            else { hex_of(d, 32, hx); if (strcmp(hx, want_h)) { llog("launcher update: not the signed file, ignored"); free(b); b = NULL; } }
        }
        int got = b && su_valid(b, n) ? su_build(b, n) : 0;
        if (got > PSO_BUILD) {
            for (int quiet = 0; quiet < 4; ) { quiet = su_quiet() ? quiet + 1 : 0; Sleep(250); }      /* a calm second first (and never while playing) */
            g_updating = 1; PostMessage(g_wnd, WM_REFRESH, 3, 0);
            int ok = 0; DWORD wr = 0;
            if (su_quiet()) {
                HANDLE f = CreateFileW(nw, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                if (f != INVALID_HANDLE_VALUE) { ok = WriteFile(f, b, (DWORD)n, &wr, NULL) && wr == n && FlushFileBuffers(f); CloseHandle(f); }
                if (ok) {
                    DeleteFileW(old);
                    ok = MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING);                 /* this running file steps aside ... */
                    if (ok && !MoveFileExW(nw, exe, MOVEFILE_REPLACE_EXISTING)) { MoveFileExW(old, exe, MOVEFILE_REPLACE_EXISTING); ok = 0; }   /* ... or goes back */
                }
                DeleteFileW(nw);
            }
            if (ok) {
                set_status(-1, L"Launcher update found \x2022 build %d to build %d \x2022 restarting...", PSO_BUILD, got);
                Sleep(1300);
                wchar_t cmd[MAX_PATH + 32], dir[MAX_PATH]; swprintf(cmd, MAX_PATH + 32, L"\"%ls\" --updated", exe);
                wcscpy(dir, exe); { wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0; }
                STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
                if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); free(b);
                    if (g_wnd) PostMessage(g_wnd, WM_APP + 0x78, 0, 0); else ExitProcess(0);
                    return 0;
                }
                set_status(-1, L"The launcher was updated. Close it and open it again to use the new one.");
                g_updating = 0; PostMessage(g_wnd, WM_REFRESH, 3, 0); free(b); return 0;
            }
            g_updating = 0; PostMessage(g_wnd, WM_REFRESH, 3, 0);        /* (a folder Windows won't let us write in: stay as we are) */
            free(b); return 0;
        }
        int answered = b != NULL; free(b);
        /* nothing newer (or no answer): look again later, in case the launcher is left open for hours */
        for (int t = 0; t < (answered || round > 3 ? 30 * 60 : 60); t++) Sleep(1000);
    }
}

static int steam_running(void);
static DWORD WINAPI side_worker(LPVOID u) {
    (void)u;
    for (int t = 0; t < 1800 && !steam_running(); t++) Sleep(2000);   /* build 26: Steam's files are read only once Steam runs */
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
    InitializeCriticalSection(&g_cs); InitializeCriticalSection(&g_imgcs); InitializeCriticalSection(&g_selfcs);
    { wchar_t me[MAX_PATH], old[MAX_PATH + 8]; if (GetModuleFileNameW(NULL, me, MAX_PATH)) { swprintf(old, MAX_PATH + 8, L"%ls.old", me); DeleteFileW(old); } }   /* left by an update */
    (void)g_build_tag;
    { ULONG_PTR tok; GpStartupInput gi = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&tok, &gi, NULL); }
    /* folders */
    wchar_t la[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) wcscpy(la, L"C:\\");
    swprintf(g_base, MAX_PATH, L"%ls\\UE4PSO", la); CreateDirectoryW(g_base, NULL);
    swprintf(g_game, MAX_PATH, L"%ls\\Game", g_base);
    { wchar_t p[MAX_PATH * 2]; swprintf(p, MAX_PATH * 2, L"%ls\\installed.txt", g_base);
      g_installed_once = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
    { wchar_t ini[MAX_PATH]; GetModuleFileNameW(NULL, ini, MAX_PATH);
      wchar_t *s = wcsrchr(ini, L'\\'); if (s) wcscpy(s + 1, L"PSOLauncher.ini");
      FILE *f = _wfopen(ini, L"r"); char line[200];
      if (f) { while (fgets(line, sizeof line, f)) if (!strncmp(line, "server=", 7)) {
                   line[strcspn(line, "\r\n ")] = 0; MultiByteToWideChar(CP_UTF8, 0, line + 7, -1, g_server, 128); g_from_ini = 1; }
               fclose(f); } }
    {   wchar_t def[MAX_PATH]; wcscpy(def, g_game);
        load_settings();                                                 /* may move g_game to the folder the player chose */
        wchar_t droot[4] = { g_game[0], L':', L'\\', 0 };
        if (_wcsicmp(def, g_game) && GetFileAttributesW(droot) == INVALID_FILE_ATTRIBUTES) {      /* that drive is not there (unplugged?) */
            wchar_t msg[900]; swprintf(msg, 900, L"The game folder is on a drive that isn't connected right now:\n%ls\n\nPlug the drive in and start the launcher again.\n\nOr press Yes to forget that folder and install the game again in the standard place.", g_game);
            if (MessageBoxW(NULL, msg, L"PSO UE4 Launcher", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return 0;
            wcscpy(g_game, def); g_installed_once = 0; save_settings();
        }
        if (!_wcsicmp(def, g_game)) CreateDirectoryW(g_game, NULL); }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    load_cache();
    { wchar_t bp[MAX_PATH * 2]; swprintf(bp, MAX_PATH * 2, L"%ls\\banner.img", g_base);
      g_banner = load_image_file(bp);
      if (!g_banner) g_banner = load_image_mem(HERO_JPG, sizeof HERO_JPG);
      g_backdrop = load_image_mem(BACKDROP_JPG, sizeof BACKDROP_JPG);
      g_logo = load_image_mem(LOGO_PNG, sizeof LOGO_PNG); }
    g_ses = WinHttpOpen(L"UE4PSOLauncher/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!g_ses) g_ses = WinHttpOpen(L"UE4PSOLauncher/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (g_ses) { DWORD mc = 16; WinHttpSetOption(g_ses, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &mc, sizeof mc); }   /* big files come in 6 pieces at once */
    WinHttpSetTimeouts(g_ses, 10000, 10000, 15000, 30000);

    HDC sdc = GetDC(NULL); g_dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(NULL, sdc);
    {   /* the heading font travels inside the exe; if Windows won't take it, everything falls back to Segoe UI */
        DWORD n1 = 0, n2 = 0;
        HANDLE fa = AddFontMemResourceEx((void *)FONT_SG_BOLD, sizeof FONT_SG_BOLD, NULL, &n1), fb = AddFontMemResourceEx((void *)FONT_SG_MEDIUM, sizeof FONT_SG_MEDIUM, NULL, &n2);
        g_fonts_ok = fa && fb && n1 && n2;
        BOOL an = TRUE; if (SystemParametersInfoW(0x1042 /* SPI_GETCLIENTAREAANIMATION */, 0, &an, 0)) g_sys_motion = an != 0;
        g_motion = g_sys_motion && g_anim_pref;      /* "show animations in Windows" off: no opening, still dots */
    }
    make_fonts();
#ifdef PSO_SHOT
    {   /* PSOLauncher --shot <file.bmp> <seconds into the opening, or 9 for the finished window> <state 0-8> <dpi> : draws one picture and exits */
        int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2 && !wcscmp(argv[1], L"--linktest")) {      /* test of the Discord link flow against a local server */
            wcscpy(g_server, L"127.0.0.1");
            g_ses = WinHttpOpen(L"UE4PSOLauncher/2.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
            if (g_ses) { DWORD mc = 16; WinHttpSetOption(g_ses, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &mc, sizeof mc); }
            g_steam.ok = 1; g_steam.sid = argc > 2 ? _wcstoui64(argv[2], NULL, 10) : 0;
            link_thread(NULL);
            wprintf(L"FINAL %d code=[%ls]\n", g_link_state, g_link_code);
            return 0;
        }
        if (argv && argc >= 3 && !wcscmp(argv[1], L"--pick")) {          /* test of the folder rules: --pick <folder> */
            g_test_pick = argv[2]; g_state = ST_NEED; g_installed_once = 0; g_total = 4831838208LL; g_done = 0;
            wchar_t before[MAX_PATH]; wcscpy(before, g_game);
            choose_install_dir(NULL);
            wprintf(L"picked [%ls] -> game folder [%ls]%ls reloc=%d\n", argv[2], g_game, wcscmp(before, g_game) ? L"" : L" (unchanged)", g_reloc);
            wcscpy(g_game, L"?"); load_settings(); wprintf(L"  read back from the settings file: [%ls] stayopen=%d animations=%d\n", g_game, g_stay_open, g_anim_pref);
            return 0;
        }
        if (argv && argc >= 3 && !wcscmp(argv[1], L"--shot")) {
            double st = argc > 3 ? _wtof(argv[3]) : 9; int state = argc > 4 ? _wtoi(argv[4]) : ST_READY; if (argc > 5) { g_dpi = _wtoi(argv[5]); make_fonts(); }
            g_shot_t = st; g_intro_on = st < INTRO_END; g_state = state; g_online = state != ST_OFFLINE && state != ST_CONNECTING; g_lobbies = g_online ? 3 : -1; g_players = g_online ? 14 : -1; g_version = 134;
            g_notes = _wcsdup(L"Old UE4 Body option\n- New option in Customization, Body Size: Old UE4 Body\n- It loads instantly in matches and shows on your menu character\n- GK Net Transparency now works for goalkeepers\n- Fixed the old UE4 training bug\n\nIf some customization options don't work it means they weren't a part of the old character model.");
            g_steam.ok = state != ST_CONNECTING; wcscpy(g_steam.name, L"strawZ"); g_steam.minutes = 61234; g_steam.last_played = (long long)_time64(NULL) - 3600;
            if (state == ST_DOWNLOADING) { g_total = 4831838208LL; g_done = 1932735283LL; g_speed = 23.5 * 1048576; wcscpy(g_status, L"Downloading ProSoccerOnline-WindowsNoEditor.pak"); }
            else wcscpy(g_status, state == ST_READY ? L"Up to date \x2022 version 134" : state == ST_NEED ? L"Update available: version 135 (3 files, 1.2 MB)" : state == ST_OFFLINE ? L"The server didn't answer. Check your connection and press Retry." : L"Connecting to the server...");
            const wchar_t *fl = argc > 6 ? argv[6] : L"";      /* u = an update (already installed), m = mouse over the window, s = settings open, o<seconds> = closing */
            g_installed_once = state != ST_NEED || wcschr(fl, L'u') != NULL; g_settings = wcschr(fl, L's') != NULL;
            if (state == ST_NEED && !g_installed_once) wcscpy(g_status, L"Ready to install \x2022 version 134 \x2022 212 file(s), 4.50 GB");
            if (wcschr(fl, L'o')) { g_outro_on = 1; g_shot_out = _wtof(wcschr(fl, L'o') + 1); g_intro_on = 0; }
            layout_wh(S(1040), S(640)); dots_setup(); g_mouse.x = S(700); g_mouse.y = S(420); g_mouse_in = wcschr(fl, L'm') != NULL; for (int i = 0; i < 90; i++) dots_step(1.0 / 60);
            BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), L.W, -L.H, 1, 32, BI_RGB, 0, 0, 0, 0, 0 } }; void *bits = NULL;
            HDC m = CreateCompatibleDC(NULL); HBITMAP bm = CreateDIBSection(m, &bi, DIB_RGB_COLORS, &bits, NULL, 0); SelectObject(m, bm);
            compose(m, (uint32_t *)bits); GdiFlush();
            BITMAPFILEHEADER fh = { 0x4D42, (DWORD)(sizeof fh + sizeof bi.bmiHeader + (DWORD)L.W * L.H * 4), 0, 0, sizeof fh + sizeof bi.bmiHeader };
            FILE *f = _wfopen(argv[2], L"wb"); if (f) { fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(bits, 4, (size_t)L.W * L.H, f); fclose(f); }
            return 0;
        }
    }
#endif
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
    g_intro0 = now_s(); g_intro_on = g_motion;
    ShowWindow(g_wnd, show); UpdateWindow(g_wnd);
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, side_worker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, selfupd_thread, NULL, 0, NULL));
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) return 0; TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (anim_active(g_wnd)) {
            double t0 = now_s(); DwmFlush();                               /* wait for the screen's next refresh: one frame per refresh, evenly spaced */
            if (now_s() - t0 < 0.0015) Sleep(2);                           /* (if Windows didn't wait, don't spin) */
            frame(g_wnd);
        } else { g_frame_last = 0; MsgWaitForMultipleObjects(0, NULL, FALSE, 250, QS_ALLINPUT); }      /* nothing moving: sleep until something happens */
    }
}

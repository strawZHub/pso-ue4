/* Coloured name tags (every player's game) - v7, no hooks.
 * A background thread finds the game's "UserID" text widgets, reads their text, and for tags the
 * server listed it changes the colour value the widget is drawn with. It never calls game code and
 * never patches the game, so it can't interfere with menus or matches. Rainbow = colour cycles. */
#include <math.h>
typedef struct { wchar_t tag[20]; int mode; float rgb[3]; } TagCol;      /* mode 1 colour, 2 rainbow */
#define MAXTAG 256
static TagCol g_tab[2][MAXTAG]; static int g_tabn[2]; static volatile int g_tabi; static volatile LONG g_tab_ver;
static void clown3d_tick(void); static void goalvid_tick(void); static void region_tick(void);
static volatile LONG g_bc, g_bc_rt; static volatile DWORD g_hb_tick; static DWORD g_gt_id;
static char g_tsrv[128];
static uintptr_t g_tb_class; static int32_t g_text_off = -1, g_col_off = -1;
static int32_t g_sw_off = -1, g_sc_off = -1;      /* UTextBlock -> STextBlock pointer, STextBlock -> colour value */

static float srgb_lin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static void rainbow_rgb(DWORD now, float *o) {
    float h = (float)(now % 3000) / 500.0f, x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f), r = 0, g = 0, b = 0;
    switch ((int)h) { case 0: r = 1; g = x; break; case 1: r = x; g = 1; break; case 2: g = 1; b = x; break;
                      case 3: g = x; b = 1; break; case 4: r = x; b = 1; break; default: r = 1; b = x; }
    o[0] = srgb_lin(r); o[1] = srgb_lin(g); o[2] = srgb_lin(b);
}
static void tags_parse(char *txt) {
    int w = !g_tabi, n = 0;
    for (char *line = txt, *nx; line && *line && n < MAXTAG; line = nx) {
        nx = strchr(line, '\n'); if (nx) *nx++ = 0;
        line[strcspn(line, "\r")] = 0;
        char *tab = strchr(line, '\t'); if (!tab) continue; *tab++ = 0;
        TagCol *t = &g_tab[w][n]; memset(t, 0, sizeof *t);
        if (!MultiByteToWideChar(CP_UTF8, 0, tab, -1, t->tag, 20) || !t->tag[0]) continue;
        if (!strcmp(line, "rainbow")) t->mode = 2;
        else if (strlen(line) == 6) {
            unsigned v = (unsigned)strtoul(line, NULL, 16); t->mode = 1;
            t->rgb[0] = srgb_lin(((v >> 16) & 255) / 255.0f); t->rgb[1] = srgb_lin(((v >> 8) & 255) / 255.0f); t->rgb[2] = srgb_lin((v & 255) / 255.0f);
        } else continue;
        n++;
    }
    g_tabn[w] = n; g_tabi = w; InterlockedIncrement(&g_tab_ver);
}
static int tags_fetch(void) {        /* one kept-open connection (no new TLS handshake each time) */
    static HINTERNET s, c; int ok = 0; HINTERNET r = NULL;
    if (!s) {
        wchar_t host[128]; MultiByteToWideChar(CP_UTF8, 0, g_tsrv, -1, host, 128);
        if (!(s = WinHttpOpen(L"pso", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0))) return 0;
        WinHttpSetTimeouts(s, 4000, 4000, 4000, 6000);
        if (!(c = WinHttpConnect(s, host, 443, 0))) { WinHttpCloseHandle(s); s = NULL; return 0; }
    }
    if ((r = WinHttpOpenRequest(c, L"GET", L"/pso-tags", NULL, NULL, NULL, WINHTTP_FLAG_SECURE))) {
        DWORD fl = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &fl, sizeof fl);
        if (WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
            static char buf[32768]; DWORD len = 0, got = 0, code = 0, sz = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
            while (len < sizeof buf - 1 && WinHttpReadData(r, buf + len, sizeof buf - 1 - len, &got) && got) len += got;
            buf[len] = 0;
            if (code == 200 && !strncmp(buf, "#tags", 5)) {
                static char last[32768];
                if (strcmp(buf, last)) { memcpy(last, buf, len + 1); tags_parse(buf); }   /* only redo colours when the list changed */
                ok = 1;
            }
            else { static int once; if (!once++) { buf[60] = 0; logf_("tags: server gave no colour list (code %lu)", code); } }
        }
        WinHttpCloseHandle(r);
    }
    if (!ok) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); s = c = NULL; }
    return ok;
}
static int wr_(uintptr_t a, const void *b, SIZE_T n) { SIZE_T p = 0; return WriteProcessMemory(g_proc, (LPVOID)a, b, n, &p) && p == n; }
static int text_of(uintptr_t td, wchar_t *out, int cap) {
    if (td < 0x10000) return 0;
    for (int off = 8; off <= 0x50; off += 8) {
        TArr s;
        if (!rd(td + off, &s, sizeof s)) return 0;
        if (s.num < 2 || s.num > cap || s.max < s.num || s.max > 4096 || s.ptr < 0x10000 || (s.ptr & 1)) continue;
        if (!rd(s.ptr, out, (SIZE_T)s.num * 2) || out[s.num - 1]) continue;
        int ok = 1; for (int i = 0; i < s.num - 1; i++) if (out[i] < 0x20) ok = 0;
        if (ok) return 1;
    }
    return 0;
}
/* one tag widget we look after */
typedef struct { uintptr_t obj, sw; int32_t idx; int32_t serial; uintptr_t td; LONG ver; int mode; float rgb[3], def[4], cur[4]; DWORD chk; } TW;
#define MAXTW 2048
static TW g_tw[MAXTW]; static int g_ntw;
static uintptr_t obj_item_rd(int32_t idx) {
    uintptr_t chunks, chunk; int32_t num;
    if (!rdp(g_exe + RVA_GOBJ_OBJECTS, &chunks) || !rd(g_exe + RVA_GOBJ_OBJECTS + 0x14, &num, 4) || idx < 0 || idx >= num) return 0;
    if (!rdp(chunks + 8ULL * (idx >> 16), &chunk)) return 0;
    return chunk + 24ULL * (idx & 0xFFFF);
}
static int alive(TW *t) {
    uintptr_t it = obj_item_rd(t->idx), o = 0, cls = 0; int32_t fl = 0, ser = 0;
    return it && rdp(it, &o) && o == t->obj && rd(it + 8, &fl, 4) && !(fl & 0x30000000) && rd(it + 0x10, &ser, 4) && ser == t->serial
        && rdp(o + 0x10, &cls) && cls == g_tb_class;
}
/* learn where the drawn colour lives: UTextBlock holds a pointer to its Slate STextBlock, which holds
 * the colour (FSlateColor = 4 floats + a rule byte) - found by matching the widget's own colour value */
static int learn_layout(uintptr_t o) {
    static int tries;
    { uintptr_t p = 0, vt = 0; float c2[4], u[4]; uint8_t r2 = 9;           /* this game build: Slate widget at +216, colour at +1424 */
      if (rd(o + g_col_off, u, 16) && rdp(o + 216, &p) && p > 0x10000 && rdp(p, &vt) && vt >= g_exe && vt <= g_exe + 0x4A00000
          && rd(p + 1424, c2, 16) && rd(p + 1424 + 16, &r2, 1) && r2 <= 1 && c2[3] >= 0 && c2[3] <= 1.01f) {
          if (g_sw_off < 0) { g_sw_off = 216; g_sc_off = 1424; logf_("tags: widget layout (known) 216/1424"); }
          return g_sw_off == 216 && g_sc_off == 1424; } }
    float col[4]; uint8_t rule;
    if (!rd(o + g_col_off, col, 16) || !rd(o + g_col_off + 16, &rule, 1)) return 0;
    unsigned char ob[0x400], sb[0x800];
    if (!rd(o, ob, sizeof ob)) return 0;
    uintptr_t tgt = 0; int found_p = -1, found_c = -1, nfound = 0, nptr = 0;
    for (int po = 0x28; po + 16 <= (int)sizeof ob; po += 8) {
        uintptr_t p = *(uintptr_t *)(ob + po), ref = *(uintptr_t *)(ob + po + 8), vt = 0;
        if (p < 0x10000 || (p & 7) || ref < 0x10000 || !rdp(p, &vt) || vt < g_exe || vt > g_exe + 0x4A00000) continue;
        if (p == tgt) { if (found_p < 0 || po < found_p) {} continue; }          /* same Slate widget seen through a 2nd pointer */
        if (!rd(p, sb, sizeof sb)) continue;
        nptr++;
        for (int co = 0x30; co + 24 <= (int)sizeof sb; co += 4)
            if (!memcmp(sb + co, col, 16) && sb[co + 16] == rule) {
                if (!nfound || p != tgt || co != found_c) nfound++;
                if (nfound == 1) { tgt = p; found_p = po; found_c = co; }
            }
    }
    if (tries < 6) { tries++; logf_("tags: layout check: colour %.2f %.2f %.2f %.2f rule %d, %d slate ptrs, %d matches (%d,%d)", col[0], col[1], col[2], col[3], rule, nptr, nfound, found_p, found_c); }
    if (nfound == 1 && (found_p < 0x60 || found_p != 216)) nfound = 0;     /* anything else is a false match */                     /* a pointer that early is the widget's slot, not its Slate widget */
    if (nfound != 1) {                                                   /* fall back to the layout this game build is known to use */
        static int fails; uintptr_t p = 0, vt = 0; float c2[4]; uint8_t r2 = 9;
        if (++fails >= 3 && g_sw_off < 0 && rdp(o + 216, &p) && p > 0x10000 && rdp(p, &vt) && vt >= g_exe && vt <= g_exe + 0x4A00000
            && rd(p + 1424, c2, 16) && rd(p + 1424 + 16, &r2, 1) && r2 <= 1 && c2[3] >= 0 && c2[3] <= 1.01f) {
            g_sw_off = 216; g_sc_off = 1424; logf_("tags: widget layout (known) 216/1424"); return 1;
        }
        return 0;
    }
    if (g_sw_off < 0) { g_sw_off = found_p; g_sc_off = found_c; logf_("tags: widget layout found (%d, %d)", found_p, found_c); return 1; }
    return g_sw_off == found_p && g_sc_off == found_c;
}
static void tw_apply(TW *t, const float *want) {
    if (!memcmp(want, t->cur, 16)) return;
    uintptr_t sw = 0; float now[4];
    if (!alive(t) || !rdp(t->obj + g_sw_off, &sw) || sw != t->sw || !rd(sw + g_sc_off, now, 16) || memcmp(now, t->cur, 16)) { t->obj = 0; return; }
    if (wr_(sw + g_sc_off, want, 16)) { wr_(t->obj + g_col_off, want, 16); memcpy(t->cur, want, 16); }
}
typedef struct { int n; } TScan;
static int g_tb_seen, g_uid_seen; static char g_tb_names[400];
static int tw_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx;
    if (cls != g_tb_class) return 0;
    char nm[64]; if (!obj_name(o, nm, sizeof nm)) return 0;
    g_tb_seen++;
    if ((strstr(nm, "ID") || strstr(nm, "Id") || strstr(nm, "User") || strstr(nm, "Tag")) && strlen(g_tb_names) < 300 && !strstr(g_tb_names, nm)) { strcat(g_tb_names, nm); strcat(g_tb_names, ","); }
    if (_stricmp(nm, "UserID") && _stricmp(nm, "PlayerID")) return 0;
    g_uid_seen++;
    for (int i = 0; i < g_ntw; i++) if (g_tw[i].obj == o) return 0;
    if (g_ntw >= MAXTW) return 1;
    /* object index + serial so a reused slot is noticed */
    int32_t idx = -1; rd(o + 0x0C, &idx, 4);
    uintptr_t it = obj_item_rd(idx), chk = 0; int32_t ser = 0;
    if (!it || !rdp(it, &chk) || chk != o || !rd(it + 0x10, &ser, 4)) return 0;
    if (g_sw_off < 0 && !learn_layout(o)) return 0;
    TW *t = &g_tw[g_ntw]; memset(t, 0, sizeof *t);
    t->obj = o; t->idx = idx; t->serial = ser; t->ver = -1;
    if (!rdp(o + g_sw_off, &t->sw) || t->sw < 0x10000 || !rd(t->sw + g_sc_off, t->def, 16)) return 0;
    memcpy(t->cur, t->def, 16);
    g_ntw++;
    return 0;
}
static void ch_try_hook(void);
static void anim_setup_and_scan(void);
static BOOL CALLBACK tags_wnd_cb2(HWND h, LPARAM l) {
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid); wchar_t cls[64];
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) && !wcscmp(cls, L"UnrealWindow")) { *(DWORD *)l = tid; return FALSE; }
    return TRUE;
}
static uintptr_t g_ch_vt;
static int tb_find_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    char nm[64]; if (!obj_name(cls, nm, sizeof nm) || strcmp(nm, "Class")) return 0;
    if (obj_name(o, nm, sizeof nm) && !strcmp(nm, "TextBlock")) { *(uintptr_t *)ctx = o; return 1; }
    return 0;
}
static DWORD WINAPI tags_worker(LPVOID u) {
    (void)u;
    g_proc = GetCurrentProcess();
    if (!refl_init()) return 0;
    logf_("---- name tag colours (add-on v86) ----");
    DWORD last_fetch = 0, last_scan = 0; int logged = 0, fetched = 0;
    for (;;) {
        DWORD now = GetTickCount();
        if (!last_fetch || now - last_fetch > 30000) { last_fetch = now; if (tags_fetch()) { static int lastn = -1; if (g_tabn[g_tabi] != lastn) { lastn = g_tabn[g_tabi]; logf_("tags: colour list: %d tag(s)", lastn); } } (void)fetched; }
        if (!g_tb_class) {
            if (now - last_scan > 3000) {
                last_scan = now;
                uintptr_t found = 0;
                for_each_object(tb_find_cb, &found);
                if (found) {
                    g_tb_class = found;
                    if (!prop_off(find_prop(found, "Text"), &g_text_off) || !prop_off(find_prop(found, "ColorAndOpacity"), &g_col_off)) {
                        static int warned; if (!warned++) logf_("tags: text widget not ready yet, retrying"); g_tb_class = 0; Sleep(500); continue;
                    }
                    logf_("tags: ready");
                }
            }
            Sleep(200); continue;
        }
        if (!g_ch_vt) { static DWORD lh; if (now - lh > 3000) { lh = now; ch_try_hook(); } }
        if (now - last_scan > 2000) {
            anim_setup_and_scan();
            static int reports;
            last_scan = now; g_tb_seen = g_uid_seen = 0; for_each_object(tw_cb, NULL);
            static int last_seen = -1;
            if (reports < 25 && g_tb_seen != last_seen) { reports++; last_seen = g_tb_seen; logf_("tags: scan: %d text widgets, %d tag widgets, %d tracked, layout %d/%d; names: %.200s", g_tb_seen, g_uid_seen, g_ntw, g_sw_off, g_sc_off, g_tb_names); }
        }
        float rb[3]; rainbow_rgb(now, rb);
        int ti = g_tabi; LONG ver = g_tab_ver;
        for (int i = 0; i < g_ntw; i++) {
            TW *t = &g_tw[i];
            if (!t->mode && t->obj && now - t->chk < 400) continue;          /* uncoloured ones: look again a few times a second */
            t->chk = now;
            if (!t->obj || !alive(t)) { g_tw[i--] = g_tw[--g_ntw]; continue; }
            uintptr_t td = 0; rdp(t->obj + g_text_off, &td);
            if (td != t->td || ver != t->ver) {
                wchar_t w[24]; t->td = td; t->ver = ver; t->mode = 0;
                int got = text_of(td, w, 20);
                if (got) for (int k = 0; k < g_tabn[ti]; k++) if (!_wcsicmp(g_tab[ti][k].tag, w)) { t->mode = g_tab[ti][k].mode; memcpy(t->rgb, g_tab[ti][k].rgb, 12); break; }
                if (logged < 10) { logged++; char a[24]; int z = 0; for (; got && w[z] && z < 23; z++) a[z] = (char)w[z]; a[z] = 0;
                                   logf_("tags: tag widget '%s' -> %s", got ? a : "?", t->mode == 2 ? "rainbow" : t->mode ? "colour" : "normal"); }
            }
            float want[4]; memcpy(want, t->def, 16);
            if (t->mode == 1) memcpy(want, t->rgb, 12); else if (t->mode == 2) memcpy(want, rb, 12);
            if (t->mode) want[3] = 1.0f;
            tw_apply(t, want);
        }
        Sleep(50);
    }
}
static void tags_start(void) {
    char path[MAX_PATH + 32], line[256];
    snprintf(path, sizeof path, "%spso-server.txt", g_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *v;
        if (strstr(line, "server") == line && (v = strchr(line, '='))) {
            v++; while (*v == ' ') v++;
            v[strcspn(v, "\r\n ")] = 0; snprintf(g_tsrv, sizeof g_tsrv, "%s", v);
        }
    }
    fclose(f);
    if (!g_tsrv[0]) return;
    HANDLE t = CreateThread(NULL, 0, tags_worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* ---- coloured tag in the "X ( Tag ) has joined the match" chat line (every player's game).
 * The game shows that line as rich text; when it arrives, the tag part gets a colour code. */
#define CH_PE_INDEX 68
typedef void (*ch_pe_fn)(void *, void *, void *);
static ch_pe_fn g_ch_orig; static uintptr_t g_ch_fn;
static wchar_t g_chbuf[4096];
static int ch_build(const wchar_t *msg, int len, wchar_t *out, int cap) {
    const wchar_t *tail = L" ) has joined the match";
    const wchar_t *t = wcsstr(msg, tail), *open = NULL;
    if (!t) return 0;
    for (const wchar_t *p = msg; p < t; p++) if (p[0] == L' ' && p[1] == L'(') open = p;
    if (!open) return 0;
    const wchar_t *tag = open + 2; while (*tag == L' ' && tag < t) tag++;
    int tl = (int)(t - tag); if (tl <= 0 || tl >= 20) return 0;
    wchar_t tw[20]; wcsncpy(tw, tag, tl); tw[tl] = 0;
    int ti = g_tabi, mode = 0; float rgb[3] = {0};
    for (int k = 0; k < g_tabn[ti]; k++) if (!wcscmp(g_tab[ti][k].tag, tw)) { mode = g_tab[ti][k].mode; memcpy(rgb, g_tab[ti][k].rgb, 12); break; }
    if (!mode) return 0;
    int n = 0;
    #define PUT(...) do { int w_ = swprintf(out + n, cap - n, __VA_ARGS__); if (w_ < 0) return 0; n += w_; } while (0)
    /* whole line in the tag's colour (rainbow = a colour per letter), like the reference */
    PUT(L"</>");
    if (mode == 1) {
        float s_[3]; for (int c = 0; c < 3; c++) { float v = rgb[c]; s_[c] = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f; }
        PUT(L"<span color=\"#%02X%02X%02XFF\">%ls</>", (int)(s_[0] * 255 + .5f), (int)(s_[1] * 255 + .5f), (int)(s_[2] * 255 + .5f), msg);
    } else {
        int vis = 0; for (const wchar_t *p = msg; *p; p++) if (*p != L' ') vis++;
        int v = 0;
        for (const wchar_t *p = msg; *p; p++) {
            if (*p == L' ') { PUT(L" "); continue; }
            float h = vis > 1 ? 300.0f * v / (vis - 1) : 0, x = 1 - fabsf(fmodf(h / 60, 2) - 1), r = 0, g = 0, b = 0;
            switch ((int)(h / 60)) { case 0: r = 1; g = x; break; case 1: r = x; g = 1; break; case 2: g = 1; b = x; break;
                                     case 3: g = x; b = 1; break; default: r = x; b = 1; }
            PUT(L"<span color=\"#%02X%02X%02XFF\">%lc</>", (int)(r * 255), (int)(g * 255), (int)(b * 255), *p); v++;
        }
    }
    PUT(L"<span color=\"#FFFF00FF\">");
    (void)len;
    return n;
}
static void ch_param_text(uintptr_t fn, void *parms, const char *name, char *out, int cap) {     /* a text / string parameter, as plain ASCII */
    int32_t off = -1; wchar_t w[64] = L""; int z = 0;
    snprintf(out, cap, "(no such parameter)");
    if (!prop_off(find_prop(fn, name), &off) || off < 0 || off > 0x200) return;
    TArr *s = (TArr *)((char *)parms + off);
    if (!(s->num >= 1 && s->num <= 60 && s->max >= s->num && s->ptr > 0x10000 && !(s->ptr & 1) && rd(s->ptr, w, (SIZE_T)s->num * 2) && !w[s->num - 1])) {
        w[0] = 0; if (!text_of(*(uintptr_t *)s, w, 60)) { snprintf(out, cap, "(empty)"); return; }
    }
    for (; w[z] && z < cap - 1; z++) out[z] = w[z] < 128 ? (char)w[z] : '?'; out[z] = 0;
}
static int ch_str_at(uintptr_t a, wchar_t *w, int cap) {              /* is there a readable string header (pointer, length, capacity) at this address? */
    TArr s; if (!rd(a, &s, sizeof s) || s.num < 2 || s.num > cap || s.max < s.num || s.max > 4096 || s.ptr < 0x10000 || (s.ptr & 1)) return 0;
    if (!rd(s.ptr, w, (SIZE_T)s.num * 2) || w[s.num - 1]) return 0;
    for (int i = 0; i < s.num - 1; i++) if (w[i] < 0x20) return 0;
    return 1;
}
static int ch_param_wide(uintptr_t fn, void *parms, const char *name, wchar_t *w, int cap) {     /* a text / string parameter, wherever the game keeps its characters */
    int32_t off = -1; w[0] = 0;
    if (!prop_off(find_prop(fn, name), &off) || off < 0 || off > 0x200) return 0;
    uintptr_t base = (uintptr_t)parms + off, td = 0;
    if (ch_str_at(base, w, cap)) return 1;                                              /* a plain string */
    if (!rdp(base, &td) || td < 0x10000) { w[0] = 0; return 0; }
    for (int o = 8; o <= 0x90; o += 8) if (ch_str_at(td + o, w, cap)) return 1;         /* inside the text object */
    for (int o = 8; o <= 0x90; o += 8) { uintptr_t q = 0; if (!rdp(td + o, &q) || q < 0x10000 || (q & 7)) continue;      /* or one step away from it */
        for (int o2 = 0; o2 <= 0x40; o2 += 8) if (ch_str_at(q + o2, w, cap)) return 1; }
    w[0] = 0; return 0;
}
static void ch_goal(uintptr_t fn, void *parms) {                 /* a goal: note who scored, start the celebration */
    static int logs;
    { wchar_t w[48]; int got = ch_param_wide(fn, parms, "ScorerName", w, 40); memcpy(g_goal_scorer, w, sizeof w);
      static int lg; if (lg++ < 6) { char a[48]; int z = 0; for (; w[z] && z < 47; z++) a[z] = w[z] < 128 ? (char)w[z] : '?'; a[z] = 0; int32_t off = -1; prop_off(find_prop(fn, "ScorerName"), &off);
          uint64_t q[4] = {0}, d[10] = {0}; if (off >= 0 && off < 0x200) { memcpy(q, (char *)parms + off, 24); if (q[0] > 0x10000) rd((uintptr_t)q[0], d, sizeof d); }
          logf_("goal fx: scorer name %s '%s' | raw %llx %llx %llx | text object %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx", got ? "read" : "NOT read", a, (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2],
                (unsigned long long)d[0], (unsigned long long)d[1], (unsigned long long)d[2], (unsigned long long)d[3], (unsigned long long)d[4], (unsigned long long)d[5], (unsigned long long)d[6], (unsigned long long)d[7], (unsigned long long)d[8], (unsigned long long)d[9]); } }
    if (logs++ < 40) { char team[64], sc[64], as[64]; int32_t ob = -1; int wa = -1;
        ch_param_text(fn, parms, "TeamScoreText", team, sizeof team); ch_param_text(fn, parms, "ScorerName", sc, sizeof sc); ch_param_text(fn, parms, "AssisterName", as, sizeof as);
        if (prop_off(find_prop(fn, "WithAssist?"), &ob) && ob >= 0 && ob < 0x200) wa = ((unsigned char *)parms)[ob];
        logf_("goal fx: goal - team text '%s', scorer '%s', assist '%s', with assist %d", team, sc, as, wa); }
    goalfx_goal();
}
static uintptr_t g_ms_in, g_ms_out;
static void ch_hook(void *self, void *fn, void *parms) {
    if (fn && (uintptr_t)fn != g_ch_fn && (uintptr_t)fn != g_goal_fn) {
        /* Each function is looked up by name once (game thread: keep it cheap). The game reloads these functions
           on every new match, so the answers are forgotten every 20 s and a new match is picked up again. */
        static uintptr_t no[1024]; static int nno; static DWORD born;
        DWORD nowt = GetTickCount(); if (nowt - born > 20000) { memset(no, 0, sizeof no); nno = 0; born = nowt; g_ms_in = g_ms_out = 0; }
        unsigned h = (unsigned)(((uintptr_t)fn >> 4) * 2654435761u) >> 22; int known = nno >= 900;
        for (int k = 0; k < 1024 && !known; k++, h = (h + 1) & 1023) { if (no[h] == (uintptr_t)fn) known = 1; else if (!no[h]) break; }
        if (!known) { char nm[96] = "";
            obj_name((uintptr_t)fn, nm, sizeof nm);
            if (!strcmp(nm, "MP_HUD-RecieveChatMessage")) g_ch_fn = (uintptr_t)fn;
            else if (!strncmp(nm, "MP_HUD-GoalStateFadeIn", 22)) { g_goal_fn = (uintptr_t)fn; static int lg; if (lg++ < 10) logf_("goal fx: goal event found"); }
            else { if (!strcmp(nm, "MP_HUD-MatchStateFadeIn")) g_ms_in = (uintptr_t)fn; else if (!strcmp(nm, "MP_HUD-MatchStateFadeOut")) g_ms_out = (uintptr_t)fn;
                   no[h] = (uintptr_t)fn; nno++; } }
        /* the pop-up in the middle of the screen (kick-off countdown etc.) is shown and hidden by messages from the match server:
           note when each arrives, so a pop-up that stays on screen can be traced (did "hide" arrive or not?) */
        if ((uintptr_t)fn == g_ms_in || (uintptr_t)fn == g_ms_out) { static int lg; if (lg++ < 120) logf_("match pop-up: the server's \"%s\" message arrived", (uintptr_t)fn == g_ms_in ? "show" : "hide"); }
    }
    if (GetCurrentThreadId() == g_gt_id) { g_bc = 7; clown3d_tick(); g_bc = 0; }
    if (fn && (uintptr_t)fn == g_goal_fn && parms) { g_gfx_pc_hint = (uintptr_t)self; ch_goal((uintptr_t)fn, parms); }
    if (fn && (uintptr_t)fn == g_ch_fn && parms && *(uintptr_t *)self == g_ch_vt) {
        TArr *hit = NULL; TArr save;
        for (int off = 0; off + 16 <= 0x100 && !hit; off += 8) {
            TArr *s = (TArr *)((char *)parms + off); wchar_t tmp[512];
            if (s->num < 10 || s->num > 500 || s->max < s->num || s->ptr < 0x10000 || (s->ptr & 1)) continue;
            if (!rd(s->ptr, tmp, (SIZE_T)s->num * 2) || tmp[s->num - 1]) continue;
            if (!wcsstr(tmp, L"has joined the match")) continue;
            int n = ch_build(tmp, s->num - 1, g_chbuf, 4000);
            if (n > 0) { save = *s; hit = s; s->ptr = (uintptr_t)g_chbuf; s->num = n + 1; s->max = n + 1; }
        }
        g_ch_orig(self, fn, parms);
        if (hit) *hit = save;
        return;
    }
    g_ch_orig(self, fn, parms);
}
static int ch_find_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    if (!class_is(cls, "SP_Controller_C")) return 0;
    rdp(o, (uintptr_t *)ctx); return 1;
}
static void ch_try_hook(void) {
    if (g_ch_vt) return;
    uintptr_t vt = 0; for_each_object(ch_find_cb, &vt);
    if (!vt || vt < g_exe || vt > g_exe + 0x4A00000) return;
    uintptr_t slot = vt + 8ULL * CH_PE_INDEX, cur; DWORD old;
    if (!rdp(slot, &cur) || cur < g_exe + 0x1000 || cur >= g_exe + 0x1000 + 0x3161b97) { logf_("chat colours: unexpected layout, off"); g_ch_vt = 1; return; }
    g_ch_orig = (ch_pe_fn)cur;
    if (!VirtualProtect((LPVOID)slot, 8, PAGE_READWRITE, &old)) return;
    g_ch_vt = vt;
    *(uintptr_t *)slot = (uintptr_t)ch_hook;
    VirtualProtect((LPVOID)slot, 8, old, &old);
    logf_("chat colours: ready");
}

/* ---- moving rainbow in join lines.
 * A background scan notes chat lines that got a rainbow; on the game's own thread (a light hook on
 * widget events, at most 8 times a second) each such line younger than 25 s gets its colours shifted:
 * the game's own Conv_StringToText + RichTextBlock.SetText are used, the same calls the game makes. */
typedef struct { uintptr_t obj; int32_t idx, serial; DWORD born; wchar_t msg[200]; } RLine;
#define MAXRL 24
static RLine g_rl[MAXRL]; static volatile LONG g_nrl_lines; static CRITICAL_SECTION g_rlcs; static int g_rl_init;
static uintptr_t g_rtb_cls, g_fn_settext, g_fn_s2t, g_kt_cdo; static int32_t g_rtb_text = -1;
static uintptr_t g_uw_vt; static ch_pe_fn g_uw_orig; static DWORD g_anim_last;
static int rl_alive(RLine *r) {
    uintptr_t it = obj_item_rd(r->idx), o = 0; int32_t fl = 0, ser = 0;
    return it && rdp(it, &o) && o == r->obj && rd(it + 8, &fl, 4) && !(fl & 0x30000000) && rd(it + 0x10, &ser, 4) && ser == r->serial;
}
static int rainbow_line(const wchar_t *msg, float shift, wchar_t *out, int cap) {
    int n = 0, vis = 0, v = 0;
    for (const wchar_t *p = msg; *p; p++) if (*p != L' ') vis++;
    for (const wchar_t *p = msg; *p; p++) {
        if (*p == L' ') { if (n + 2 >= cap) return 0; out[n++] = L' '; continue; }
        float h = fmodf((vis > 1 ? 300.0f * v / (vis - 1) : 0) + shift, 360.0f), x = 1 - fabsf(fmodf(h / 60, 2) - 1), r = 0, g = 0, b = 0;
        switch ((int)(h / 60)) { case 0: r = 1; g = x; break; case 1: r = x; g = 1; break; case 2: g = 1; b = x; break;
                                 case 3: g = x; b = 1; break; case 4: r = x; b = 1; break; default: r = 1; b = x; }
        int w = swprintf(out + n, cap - n, L"<span color=\"#%02X%02X%02XFF\">%lc</>", (int)(r * 255), (int)(g * 255), (int)(b * 255), *p);
        if (w < 0) return 0; n += w; v++;
    }
    return n;
}
static void anim_pump(void) {
    DWORD now = GetTickCount();
    if (now - g_anim_last < 120 || !g_nrl_lines || !g_fn_settext || !g_fn_s2t || !g_kt_cdo) return;
    g_anim_last = now;
    static wchar_t buf[6000];
    EnterCriticalSection(&g_rlcs);
    for (int i = 0; i < g_nrl_lines; i++) {
        RLine *r = &g_rl[i];
        if (now - r->born > 25000 || !rl_alive(r)) continue;
        int n = rainbow_line(r->msg, (float)((now / 8) % 360), buf, 5900);
        if (n <= 0) continue;
        struct { uintptr_t ptr; int32_t num, max; uintptr_t text[3]; uint8_t spare[32]; } P1;
        memset(&P1, 0, sizeof P1); P1.ptr = (uintptr_t)buf; P1.num = n + 1; P1.max = n + 1;
        ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE);                         /* UObject::ProcessEvent */
        upe((void *)g_kt_cdo, (void *)g_fn_s2t, &P1);                          /* FText made by the game */
        if (!P1.text[0] || !P1.text[1]) continue;
        struct { uintptr_t text[3]; uint8_t spare[32]; } P2; memset(&P2, 0, sizeof P2); memcpy(P2.text, P1.text, 24);
        upe((void *)r->obj, (void *)g_fn_settext, &P2);                          /* the widget keeps its own copy */
        InterlockedDecrement((volatile LONG *)(P1.text[1] + 8));               /* drop ours (widget still holds one) */
    }
    LeaveCriticalSection(&g_rlcs);
}
static void theme_pump(void *self);
static void theme_binding_fix(void *self, void *fn, void *parms);
static void goaltab_tick(void *self);
static void uw_hook(void *self, void *fn, void *parms) {
    g_uw_orig(self, fn, parms);
    if (GetCurrentThreadId() != g_gt_id) { theme_binding_fix(self, fn, parms); return; }
    g_bc = 2; theme_binding_fix(self, fn, parms);
    g_bc = 3; anim_pump(); g_bc = 4; theme_pump(self); g_bc = 5; goaltab_tick(self); g_bc = 8; goalvid_tick(); region_tick(); g_bc = 6; clown3d_tick(); g_bc = 0;
}
typedef struct { int pass; } AnimFind;
static int anim_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; char nm[96];
    if (!g_rtb_cls && class_is(cls, "Class") && obj_name(o, nm, sizeof nm) && !strcmp(nm, "RichTextBlock")) g_rtb_cls = o;
    else if (!g_kt_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__KismetTextLibrary")) g_kt_cdo = o;
    else if ((!g_fn_settext || !g_fn_s2t) && class_is(cls, "Function") && obj_name(o, nm, sizeof nm)) {
        uintptr_t outer = 0; char on[96] = "";
        if (rdp(o + 0x20, &outer)) obj_name(outer, on, sizeof on);
        if (!strcmp(nm, "SetText") && !strcmp(on, "RichTextBlock")) g_fn_settext = o;
        if (!strcmp(nm, "Conv_StringToText") && !strcmp(on, "KismetTextLibrary")) g_fn_s2t = o;
    }
    else if (!g_uw_vt && g_rtb_cls && cls != g_rtb_cls) {
        uintptr_t c = cls; char cn[96];
        for (int d = 0; c && d < 12; d++) { if (obj_name(c, cn, sizeof cn) && !strcmp(cn, "UserWidget") && !is_cdo(o)) { rdp(o, &g_uw_vt); break; } uintptr_t s = 0; rdp(c + 0x40, &s); c = s; }
    }
    return 0;
}
static void plain_of(const wchar_t *in, wchar_t *out, int cap) {        /* drop the <span ...> and </> codes */
    int n = 0;
    for (const wchar_t *p = in; *p && n < cap - 1; ) {
        if (*p == L'<') { const wchar_t *e = wcschr(p, L'>'); if (!e) break; p = e + 1; continue; }
        out[n++] = *p++;
    }
    out[n] = 0;
}
static int rl_scan_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx;
    if (cls != g_rtb_cls) return 0;
    uintptr_t td = 0; static wchar_t w[4200];
    if (!rdp(o + g_rtb_text, &td) || td < 0x10000) return 0;
    TArr s; int got = 0;
    for (int off = 8; off <= 0x50 && !got; off += 8) {
        if (!rd(td + off, &s, sizeof s) || s.num < 30 || s.num > 4096 || s.max < s.num || s.ptr < 0x10000 || (s.ptr & 1)) continue;
        if (rd(s.ptr, w, (SIZE_T)s.num * 2) && !w[s.num - 1]) got = 1;
    }
    if (!got || !wcsstr(w, L"has joined the match")) return 0;
    int spans = 0; for (const wchar_t *p = w; (p = wcsstr(p, L"<span")); p++) spans++;
    if (spans < 8) return 0;                                                 /* only rainbow lines */
    EnterCriticalSection(&g_rlcs);
    int have = 0; for (int i = 0; i < g_nrl_lines; i++) if (g_rl[i].obj == o) have = 1;
    if (!have) {
        if (g_nrl_lines >= MAXRL) { memmove(g_rl, g_rl + 1, sizeof(RLine) * (MAXRL - 1)); g_nrl_lines--; }
        RLine *r = &g_rl[g_nrl_lines]; memset(r, 0, sizeof *r);
        int32_t idx = -1; rd(o + 0x0C, &idx, 4);
        uintptr_t it = obj_item_rd(idx); int32_t ser = 0;
        if (it && rd(it + 0x10, &ser, 4)) {
            r->obj = o; r->idx = idx; r->serial = ser; r->born = GetTickCount(); plain_of(w, r->msg, 200);
            g_nrl_lines++;
        }
    }
    LeaveCriticalSection(&g_rlcs);
    return 0;
}
static void anim_setup_and_scan(void) {
    if (!g_rl_init) { InitializeCriticalSection(&g_rlcs); g_rl_init = 1; }
    if (!g_gt_id) { DWORD t = 0; EnumWindows(tags_wnd_cb2, (LPARAM)&t); g_gt_id = t; }
    if (!g_rtb_cls || !g_fn_settext || !g_fn_s2t || !g_kt_cdo || !g_uw_vt) {
        for_each_object(anim_cb, NULL);
        if (g_rtb_cls && g_rtb_text < 0) prop_off(find_prop(g_rtb_cls, "Text"), &g_rtb_text);
        if (g_uw_vt && !g_uw_orig && g_gt_id && g_fn_settext && g_fn_s2t && g_kt_cdo && g_rtb_text >= 0) {
            uintptr_t slot = g_uw_vt + 8ULL * CH_PE_INDEX, cur; DWORD old;
            if (rdp(slot, &cur) && cur >= g_exe + 0x1000 && cur < g_exe + 0x1000 + 0x3161b97 && VirtualProtect((LPVOID)slot, 8, PAGE_READWRITE, &old)) {
                g_uw_orig = (ch_pe_fn)cur; *(uintptr_t *)slot = (uintptr_t)uw_hook; VirtualProtect((LPVOID)slot, 8, old, &old);
                logf_("chat rainbow: ready");
            }
        }
        return;
    }
    if (g_rtb_text >= 0) for_each_object(rl_scan_cb, NULL);
}

/* The region lists in Custom Lobbies say "NA (Ohio)", but the server is in Virginia. The three drop-down lists there
 * are refilled through the game's own list functions (clear, add, select) with the same entries, "Ohio" reading
 * "Virginia". Only the label changes: the game sends the entry's position in the list, never its text. */
#define RG_MAX 8
typedef struct { uintptr_t o; int32_t idx, ser; int done, tries; } RgCombo;
static RgCombo g_rg[RG_MAX]; static volatile LONG g_nrg; static CRITICAL_SECTION g_rgcs; static int g_rg_logs;
static uintptr_t g_rg_fn[6]; static const char *RG_FN[6] = { "GetOptionCount", "GetOptionAtIndex", "GetSelectedIndex", "ClearOptions", "AddOption", "SetSelectedIndex" };
enum { RG_COUNT, RG_AT, RG_SEL, RG_CLEAR, RG_ADD, RG_SETSEL };
static int rg_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; static uintptr_t ccls;
    if (!ccls) { if (!class_is(cls, "ComboBoxString")) return 0; ccls = cls; }
    if (cls != ccls) return 0;
    char n[64]; if (!obj_name(o, n, sizeof n) || (strcmp(n, "RegionPopup") && strcmp(n, "CreateSessionRegion") && strcmp(n, "Region"))) return 0;
    int in_lobbies = 0; uintptr_t ow = o;
    for (int d = 0; d < 6 && rdp(ow + 0x20, &ow) && ow; d++) if (class_is(obj_class(ow), "CustomLobbies_C")) { in_lobbies = 1; break; }
    if (!in_lobbies) return 0;
    int32_t fl = 0, ix = -1; rd(o + 0x0C, &ix, 4); uintptr_t it = obj_item_rd(ix); if (!it || !rd(it + 8, &fl, 4) || (fl & 0x30000000)) return 0;
    EnterCriticalSection(&g_rgcs);
    int known = 0; for (int i = 0; i < g_nrg; i++) if (g_rg[i].o == o && obj_alive3(o, g_rg[i].idx, g_rg[i].ser)) known = 1;
    if (!known) { int k = -1; for (int i = 0; i < g_nrg; i++) if (!obj_alive3(g_rg[i].o, g_rg[i].idx, g_rg[i].ser)) { k = i; break; }
        if (k < 0 && g_nrg < RG_MAX) k = g_nrg++;
        if (k >= 0) { g_rg[k].o = o; obj_id(o, &g_rg[k].idx, &g_rg[k].ser); g_rg[k].done = 0; g_rg[k].tries = 0; } }
    LeaveCriticalSection(&g_rgcs);
    return 0;
}
static int rg_first(uintptr_t fn) { uintptr_t p = 0; int32_t o = 0; if (rdp(fn + 0x50, &p) && p) rd(p + 0x4C, &o, 4); return o; }
static void rg_call(uintptr_t o, int f) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)o, (void *)g_rg_fn[f], GP); }
static int rg_int(uintptr_t o, int f) { int r = gt_po(g_rg_fn[f], "ReturnValue"); int32_t v = -1; if (r < 0 || r > 3000) return -1; memset(GP, 0, 128); rg_call(o, f); memcpy(&v, GP + r, 4); return v; }
static int rg_one(RgCombo *c) {                                       /* game thread; 1 = finished with this list */
    uintptr_t o = c->o;
    if (!g_rg_fn[0]) { uintptr_t ch = 0; rdp(obj_class(o) + 0x48, &ch);
        for (int n = 0; ch && n < 400; n++) { char fn[64]; if (obj_name(ch, fn, sizeof fn)) for (int k = 0; k < 6; k++) if (!strcmp(fn, RG_FN[k])) g_rg_fn[k] = ch; uintptr_t nx = 0; if (!rdp(ch + 0x28, &nx)) break; ch = nx; }
        for (int k = 0; k < 6; k++) if (!g_rg_fn[k]) { if (g_rg_logs++ < 3) logf_("region name: the game's list function %s wasn't found, name left as it is", RG_FN[k]); g_rg_fn[0] = 0; return 1; } }
    int n = rg_int(o, RG_COUNT); if (n <= 0 || n > 12) return ++c->tries > 20;
    static wchar_t opt[12][64], orig[12][64]; int hit = 0, ro = gt_po(g_rg_fn[RG_AT], "ReturnValue"), po = rg_first(g_rg_fn[RG_AT]); if (ro < 0 || ro > 3000) return 1;
    for (int i = 0; i < n; i++) {
        struct { uintptr_t p; int32_t num, max; } fs = { 0, 0, 0 };
        memset(GP, 0, 128); memcpy(GP + po, &i, 4); rg_call(o, RG_AT); memcpy(&fs, GP + ro, 16);
        opt[i][0] = 0; if (!fs.p || fs.num <= 1 || fs.num > 40 || !rd(fs.p, opt[i], fs.num * 2)) return ++c->tries > 20;
        opt[i][fs.num < 63 ? fs.num : 63] = 0;
        wcscpy(orig[i], opt[i]);
        wchar_t *at = wcsstr(opt[i], L"Ohio");
        if (at) { wchar_t rest[64]; wcscpy(rest, at + 4); wcscpy(at, L"Virginia"); wcscat(at, rest); hit++; }
    }
    if (!hit) return 1;
    int sel = rg_int(o, RG_SEL);
    if (sel < 0) {                                                    /* list not shown yet: the entry the game would start on is kept by its text */
        int so2 = gt_po(obj_class(o), "SelectedOption"); struct { uintptr_t p; int32_t num, max; } fs = { 0, 0, 0 }; wchar_t cur[64] = L"";
        if (so2 >= 0 && rd(o + so2, &fs, 16) && fs.p && fs.num > 1 && fs.num <= 40 && rd(fs.p, cur, fs.num * 2)) { cur[fs.num < 63 ? fs.num : 63] = 0; for (int i = 0; i < n; i++) if (!wcscmp(cur, orig[i])) sel = i; }
    }
    memset(GP, 0, 128); rg_call(o, RG_CLEAR);
    int ao = rg_first(g_rg_fn[RG_ADD]);
    for (int i = 0; i < n; i++) { struct { uintptr_t p; int32_t num, max; } fs = { (uintptr_t)opt[i], (int32_t)wcslen(opt[i]) + 1, (int32_t)wcslen(opt[i]) + 1 };
        memset(GP, 0, 128); memcpy(GP + ao, &fs, 16); rg_call(o, RG_ADD); }
    if (sel >= 0 && sel < n) { int so = rg_first(g_rg_fn[RG_SETSEL]); memset(GP, 0, 128); memcpy(GP + so, &sel, 4); rg_call(o, RG_SETSEL); }
    char nm[64] = ""; obj_name(o, nm, sizeof nm);
    if (g_rg_logs++ < 12) logf_("region name: list '%s' refilled, %d entries, \"Ohio\" now reads \"Virginia\" (selected entry %d kept)", nm, n, sel);
    return 1;
}
static void region_tick(void) {                                       /* game thread */
    static DWORD last; DWORD now = GetTickCount(); if (g_shutdown || !g_nrg || now - last < 250) return; last = now;
    for (int i = 0; i < g_nrg; i++) { RgCombo *c = &g_rg[i]; if (c->done || !obj_alive3(c->o, c->idx, c->ser)) continue; if (rg_one(c)) c->done = 1; }
}
static DWORD WINAPI region_worker(LPVOID u) {
    (void)u; if (!refl_init()) return 0;
    for (;;) {
        Sleep(2000);
        if (g_shutdown) return 0;
        if (!g_in_menu || !g_logged_in) continue;
        int live = 0; EnterCriticalSection(&g_rgcs); for (int i = 0; i < g_nrg; i++) if (obj_alive3(g_rg[i].o, g_rg[i].idx, g_rg[i].ser)) live++; LeaveCriticalSection(&g_rgcs);
        if (live >= 3) continue;                                     /* this menu's three lists are known already */
        for_each_object(rg_cb, NULL);
    }
}
static void region_start(void) { InitializeCriticalSection(&g_rgcs); CloseHandle(CreateThread(NULL, 0, region_worker, NULL, 0, NULL)); }

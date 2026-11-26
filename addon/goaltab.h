/* goaltab.h - "Goal Effect" tab in the game's own Player Customization screen (test build).
 * Nothing is drawn by us: the tab button, its text, the panel, the grid and the picture buttons are
 * created by the game itself as copies of its "Body Size" tab (same style, font, padding, grid layout).
 * Only the text and the pictures differ. Clicking a picture previews the effect, Save keeps the choice. */
typedef struct { const char *outer, *name; uintptr_t fn; } GtFn;
enum { GT_SPAWN, GT_S2T, GT_BTN_STYLE, GT_BTN_PRESSED, GT_HOVER, GT_ADDCHILD, GT_HB_ADD, GT_HBS_SIZE, GT_HBS_PAD, GT_HBS_HA, GT_HBS_VA, GT_BS_PAD, GT_BS_HA, GT_BS_VA,
       GT_TB_TEXT, GT_TB_FONT, GT_TB_COL, GT_UG_ADD, GT_UGS_HA, GT_UGS_VA, GT_SW_SET, GT_CV_ADD, GT_CVS_LAYOUT, GT_CVS_AUTO, GT_CVS_Z, GT_ENABLE, GT_N };
static GtFn GTF[GT_N] = { {"GameplayStatics", "SpawnObject"}, {"KismetTextLibrary", "Conv_StringToText"}, {"Button", "SetStyle"}, {"Button", "IsPressed"}, {"Widget", "IsHovered"}, {"PanelWidget", "AddChild"},
    {"HorizontalBox", "AddChildToHorizontalBox"}, {"HorizontalBoxSlot", "SetSize"}, {"HorizontalBoxSlot", "SetPadding"}, {"HorizontalBoxSlot", "SetHorizontalAlignment"}, {"HorizontalBoxSlot", "SetVerticalAlignment"},
    {"ButtonSlot", "SetPadding"}, {"ButtonSlot", "SetHorizontalAlignment"}, {"ButtonSlot", "SetVerticalAlignment"},
    {"TextBlock", "SetText"}, {"TextBlock", "SetFont"}, {"TextBlock", "SetColorAndOpacity"}, {"UniformGridPanel", "AddChildToUniformGrid"}, {"UniformGridSlot", "SetHorizontalAlignment"}, {"UniformGridSlot", "SetVerticalAlignment"},
    {"WidgetSwitcher", "SetActiveWidget"}, {"CanvasPanel", "AddChildToCanvas"}, {"CanvasPanelSlot", "SetLayout"}, {"CanvasPanelSlot", "SetAutoSize"}, {"CanvasPanelSlot", "SetZOrder"}, {"Widget", "SetIsEnabled"} };
#define GT_ITEMS 40
static struct {
    volatile LONG ready, built, fns;                      /* ready: the game's widgets were found; built: our tab exists */
    uintptr_t gs_cdo, kt_cdo, hb, tabbtn, sw, bodypanel, save, cancel;
    uintptr_t mybtn, mypanel, item[GT_ITEMS]; int nitem, fx[GT_ITEMS]; int32_t idx, ser;
    int st_tab, st_save, st_cancel, st_item[GT_ITEMS]; int pending, saved, fails;
    uintptr_t tex[GFX_N]; wchar_t file[MAX_PATH], mark[MAX_PATH];
    uintptr_t prev[6]; int32_t fi[6], fs[6]; int stable, off; int32_t ii[GT_ITEMS], is[GT_ITEMS], pi, ps;              /* found widgets must be the same in two scans, and still alive when used */
} GT;
static uint8_t GP[4096];
static int gt_po(uintptr_t st, const char *p) { int32_t o = -1; return prop_off(find_prop(st, p), &o) ? o : -1; }
static int gt_psz(uintptr_t st, const char *p) { uintptr_t pr = find_prop(st, p); int32_t z = 0; if (pr) rd(pr + 0x3C, &z, 4); return z; }
static void gt_call(uintptr_t obj, int f) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)obj, (void *)GTF[f].fn, GP); }
static int gt_first(int f) { uintptr_t p = 0; int32_t o = 0; if (rdp(GTF[f].fn + 0x50, &p) && p) rd(p + 0x4C, &o, 4); return o; }      /* offset of the first parameter */
static uintptr_t gt_ret(int f) { int o = gt_po(GTF[f].fn, "ReturnValue"); uintptr_t r = 0; if (o >= 0 && o < 4000) memcpy(&r, GP + o, 8); return r; }
static uintptr_t gt_spawn(uintptr_t cls, uintptr_t outer) {
    int a = gt_po(GTF[GT_SPAWN].fn, "ObjectClass"), b = gt_po(GTF[GT_SPAWN].fn, "Outer"); if (a < 0 || b < 0 || !cls || !outer) return 0;
    memset(GP, 0, 256); memcpy(GP + a, &cls, 8); memcpy(GP + b, &outer, 8); gt_call(GT.gs_cdo, GT_SPAWN); return gt_ret(GT_SPAWN);
}
/* dst.Setter(src.prop): the game copies the value itself, so fonts, brushes etc. stay properly shared */
static int gt_copy(uintptr_t dst, uintptr_t src, const char *prop, int f) {
    int off = gt_po(obj_class(src), prop), sz = gt_psz(obj_class(src), prop), po = gt_first(f);
    if (off < 0 || sz <= 0 || po + sz > 3900) { logf_("goal tab: %s.%s not copied (%d/%d)", GTF[f].outer, prop, off, sz); return 0; }
    memset(GP, 0, po + sz + 64); if (!rd(src + off, GP + po, sz)) return 0;
    gt_call(dst, f); return 1;
}
static void gt_raw(uintptr_t dst, uintptr_t src, const char *prop) {                 /* plain values only (numbers, colours, enums), before the widget is shown */
    int off = gt_po(obj_class(src), prop), sz = gt_psz(obj_class(src), prop); uint8_t b[64];
    if (off >= 0 && sz > 0 && sz <= 64 && rd(src + off, b, sz)) wr_(dst + off, b, sz);
}
static int gt_nchild(uintptr_t panel) { int o = gt_po(obj_class(panel), "Slots"); TArr a; return o >= 0 && rd(panel + o, &a, sizeof a) && a.num > 0 && a.num < 400 ? a.num : 0; }
static uintptr_t gt_child(uintptr_t panel, int i, uintptr_t *slot) {
    int o = gt_po(obj_class(panel), "Slots"); TArr a; uintptr_t s = 0, c = 0; if (slot) *slot = 0;
    if (o < 0 || !rd(panel + o, &a, sizeof a) || i < 0 || i >= a.num || !rdp(a.ptr + 8ULL * i, &s) || !s) return 0;
    int co = gt_po(obj_class(s), "Content"); if (co < 0 || !rdp(s + co, &c)) return 0;
    if (slot) *slot = s; return c;
}
static int gt_bool(uintptr_t o, int f) { int r = gt_po(GTF[f].fn, "ReturnValue"); if (r < 0) return 0; memset(GP, 0, 64); gt_call(o, f); return GP[r] != 0; }
static int gt_clicked(uintptr_t btn, int *st) {                                       /* pressed, then let go while still over it */
    int p = gt_bool(btn, GT_BTN_PRESSED), was = *st; *st = p;
    return was && !p && gt_bool(btn, GT_HOVER);
}
static void gt_set1(uintptr_t o, int f, const void *v, int n) { int po = gt_first(f); memset(GP, 0, po + n + 64); memcpy(GP + po, v, n); gt_call(o, f); }

/* ---- finding the game's widgets (our own thread, only while in the main menu and the tab isn't there yet) */
static int gt_find_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; char nm[64], on[64];
    if (!obj_name(o, nm, sizeof nm)) return 0;
    if (!GT.fns) {
        if (!strcmp(nm, "Default__GameplayStatics")) { GT.gs_cdo = o; return 0; }
        if (!strcmp(nm, "Default__KismetTextLibrary")) { GT.kt_cdo = o; return 0; }
        static uintptr_t fcls; if (!fcls && class_is(cls, "Function")) fcls = cls;
        if (cls == fcls) { uintptr_t ou = 0; if (rdp(o + 0x20, &ou) && obj_name(ou, on, sizeof on)) for (int i = 0; i < GT_N; i++) if (!GTF[i].fn && !strcmp(nm, GTF[i].name) && !strcmp(on, GTF[i].outer)) GTF[i].fn = o; return 0; }
    }
    uintptr_t *slot = !strcmp(nm, "AccessoriesSubTabButtons") ? &GT.hb : !strcmp(nm, "BallTabButton") ? &GT.tabbtn : !strcmp(nm, "PanelSwitcher") ? &GT.sw
                    : !strcmp(nm, "BodySizePanel") ? &GT.bodypanel : !strcmp(nm, "Save") ? &GT.save : !strcmp(nm, "Cancel") ? &GT.cancel : NULL;
    if (!slot) return 0;
    uintptr_t tree = 0, uw = 0, up = 0;                                               /* must be the live Player Customization screen, not its blueprint */
    if (!rdp(o + 0x20, &tree) || !tree || !rdp(tree + 0x20, &uw) || !uw || !class_is(obj_class(uw), "PlayerCustomization_C")) return 0;
    if (!rdp(uw + 0x20, &up) || !up || !class_is(obj_class(up), "WidgetTree")) return 0;
    *slot = o; return 0;
}
static void goaltab_scan(void) {                                                      /* called every few seconds from the goal-effect thread */
    if (GT.off || GT.built || GT.ready || !g_in_menu || GT.fails > 3) return;
    GT.hb = GT.tabbtn = GT.sw = GT.bodypanel = GT.save = GT.cancel = 0;
    for_each_object(gt_find_cb, NULL);
    if (!GT.fns) { int ok = GT.gs_cdo && GT.kt_cdo; for (int i = 0; i < GT_N; i++) if (!GTF[i].fn) { ok = 0; static int lg; if (lg++ < 30) logf_("goal tab: game function %s.%s not found yet", GTF[i].outer, GTF[i].name); }
        if (ok) GT.fns = 1; else { GT.fails++; DeleteFileW(GT.mark); return; } }
    { static int lastk = -1, lg; int k = !!GT.hb | !!GT.tabbtn << 1 | !!GT.sw << 2 | !!GT.bodypanel << 3 | !!GT.save << 4 | !!g_logged_in << 5;
      if (k != lastk && lg++ < 20) { lastk = k; logf_("goal tab: looking for the Ball tab + Body Size grid: row %d, button %d, switcher %d, panel %d, save %d, logged in %d", !!GT.hb, !!GT.tabbtn, !!GT.sw, !!GT.bodypanel, !!GT.save, !!g_logged_in); } }
    if (!(GT.hb && GT.tabbtn && GT.sw && GT.bodypanel && GT.save) || !g_logged_in) { GT.stable = 0; return; }
    uintptr_t cur[6] = { GT.hb, GT.tabbtn, GT.sw, GT.bodypanel, GT.save, GT.cancel };
    memcpy(GT.prev, cur, sizeof cur);      /* found: add the tab straight away (everything is checked once more on the game's own thread right before it's used) */
    for (int i = 0; i < 6; i++) { GT.fi[i] = -1; GT.fs[i] = 0; if (cur[i]) obj_id(cur[i], &GT.fi[i], &GT.fs[i]); }
    GT.ready = 1;
}

/* ---- pictures for the grid: each effect rendered once on a night-pitch background */
static uintptr_t gt_thumb(void *ctx, int id) {
    enum { S = 256 }; static uint32_t lo[S * S]; static uint8_t rgba[S * S * 4];
    if (GT.tex[id - 1]) return GT.tex[id - 1];
    GCam c; float cp[3] = { -1250, 0, 380 }, P[3] = { 0, 0, 11 }; gcam_set(&c, cp, -11, 0, 0, 80, S);
    static const float TT[GFX_N] = { 0.13f, 0.33f, 0.33f, 0.3f, 0.42f, 0.7f, 0.37f, 0.55f, 0.3f };
    float T = GFX_DUR[id - 1] * TT[id - 1], last = 0; int first = 1, keep = id == 5 || id == 7;      /* these two leave trails: play them up to that moment */
    for (float t = keep ? 0 : T; t <= T + 0.001f; t += 1 / 60.f) { gfx_render(id, t, t - last, first, lo, S, S, &c, P, 0); last = t; first = 0; }
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        uint32_t p = lo[y * S + x]; int a = p >> 24; float h = (float)y / S;
        int bg[3]; if (h < 0.52f) { bg[0] = 14 + (int)(h * 30); bg[1] = 20 + (int)(h * 44); bg[2] = 44 + (int)(h * 60); } else { float g = (h - 0.52f) / 0.48f; bg[0] = 22 + (int)(g * 14); bg[1] = 74 + (int)(g * 40); bg[2] = 44 + (int)(g * 18); }
        uint8_t *o = rgba + (y * S + x) * 4;
        o[0] = (uint8_t)((p >> 16 & 255) + bg[0] * (255 - a) / 255); o[1] = (uint8_t)((p >> 8 & 255) + bg[1] * (255 - a) / 255); o[2] = (uint8_t)((p & 255) + bg[2] * (255 - a) / 255); o[3] = 255;
    }
    return GT.tex[id - 1] = png_texture(ctx, rgba, S, S);
}
static int gt_readtext(uintptr_t tb, wchar_t *out, int cap) { int o = gt_po(obj_class(tb), "Text"); uintptr_t td = 0; out[0] = 0; return o >= 0 && rdp(tb + o, &td) && text_of(td, out, cap); }
static void gt_settext(uintptr_t tb, const wchar_t *w) {
    int a = gt_first(GT_S2T), r = -1, to = gt_po(obj_class(tb), "Text");                  /* parameters by position: the string in, then the text out */
    { uintptr_t p1 = 0, p2 = 0; int32_t o2 = -1; if (rdp(GTF[GT_S2T].fn + 0x50, &p1) && p1 && rdp(p1 + 0x20, &p2) && p2 && rd(p2 + 0x4C, &o2, 4)) r = o2; }
    if (a < 0 || r < 16 || r > 200) { logf_("goal tab: text maker parameters not found (%d/%d)", a, r); return; }
    UStr s = ustr(w); uintptr_t text[3]; wchar_t back[48];
    memset(GP, 0, 128); memcpy(GP + a, &s, 16); gt_call(GT.kt_cdo, GT_S2T); memcpy(text, GP + r, 24);
    if (!text[0] || !text[1]) { logf_("goal tab: the game returned no text"); return; }
    gt_set1(tb, GT_TB_TEXT, text, 24);
    if (gt_readtext(tb, back, 40) && !wcscmp(back, w)) { InterlockedDecrement((volatile LONG *)(text[1] + 8)); logf_("goal tab: text set"); return; }   /* drop our reference (the widget holds its own) */
    if (to >= 0) { wr_(tb + to, text, 24); logf_("goal tab: text set directly (the normal way left it %s)", back[0] ? "different" : "empty"); }      /* our reference now belongs to the widget */
}
static uintptr_t gt_button_like(uintptr_t src, uintptr_t outer, uintptr_t tex) {      /* a new Button with src's look (and, with tex, that picture) */
    uintptr_t b = gt_spawn(obj_class(src), outer); if (!b) return 0;
    gt_raw(b, src, "ColorAndOpacity"); gt_raw(b, src, "BackgroundColor"); gt_raw(b, src, "ClickMethod"); gt_raw(b, src, "TouchMethod"); gt_raw(b, src, "PressMethod"); gt_raw(b, src, "Visibility");
    int off = gt_po(obj_class(src), "WidgetStyle"), sz = gt_psz(obj_class(src), "WidgetStyle"), po = gt_first(GT_BTN_STYLE);
    if (off < 0 || sz <= 0 || po + sz > 3900 || !rd(src + off, GP + po, sz)) { logf_("goal tab: button style not readable (%d/%d)", off, sz); return b; }
    if (tex) {
        uintptr_t sp = find_prop(obj_class(src), "WidgetStyle"), st = 0, bp, bst = 0; rdp(sp + 0x78, &st);
        static const char *br[3] = { "Normal", "Hovered", "Pressed" };
        for (int i = 0; st && i < 3; i++) { int bo = gt_po(st, br[i]); int32_t ro = g_res_off;
            if (ro < 0 && (bp = find_prop(st, br[i])) && rdp(bp + 0x78, &bst) && bst) ro = gt_po(bst, "ResourceObject");
            if (bo >= 0 && ro >= 0) memcpy(GP + po + bo + ro, &tex, 8); }
    }
    gt_call(b, GT_BTN_STYLE);
    return b;
}
static void gt_build(void *ctx) {                                                     /* game thread */
    uintptr_t tree = 0, txt0, bs0 = 0, grid0, gslot0 = 0, hslot0 = 0, pslot0 = 0;
    GT.ready = 0;
    {   uintptr_t cur[6] = { GT.hb, GT.tabbtn, GT.sw, GT.bodypanel, GT.save, GT.cancel };                  /* everything found earlier must still be the same live widget */
        static const char *nm6[6] = { "AccessoriesSubTabButtons", "BallTabButton", "PanelSwitcher", "BodySizePanel", "Save", "Cancel" };
        for (int i = 0; i < 6; i++) { char n[64] = ""; if (!cur[i]) continue;
            if (!obj_alive3(cur[i], GT.fi[i], GT.fs[i]) || !obj_name(cur[i], n, sizeof n) || strcmp(n, nm6[i])) { static int lg; if (lg++ < 10) logf_("goal tab: '%s' changed since it was found (now '%s'), looking again", nm6[i], n); GT.stable = 0; memset(GT.prev, 0, sizeof GT.prev); return; } }
    }
    { FILE *mf = _wfopen(GT.mark, L"w"); if (mf) { fputs("building\n", mf); fclose(mf); } }             /* removed again when the tab is in; left behind = it crashed */
    logf_("goal tab: building (button %p, row %p, switcher %p, panel %p)", (void *)GT.tabbtn, (void *)GT.hb, (void *)GT.sw, (void *)GT.bodypanel);
    if (!rdp(GT.tabbtn + 0x20, &tree) || !tree) return;
    for (int i = 0; i < gt_nchild(GT.hb); i++) { uintptr_t s, c = gt_child(GT.hb, i, &s); if (c == GT.tabbtn) hslot0 = s; if (c && class_is(obj_class(c), "Button")) { uintptr_t t = gt_child(c, 0, NULL); char n[64]; wchar_t tw[48]; (void)n; if (t && gt_readtext(t, tw, 40) && !wcscmp(tw, L"Goal Effect")) { logf_("goal tab: already there"); GT.fails = 9; DeleteFileW(GT.mark); return; } } }
    txt0 = gt_child(GT.tabbtn, 0, &bs0); grid0 = gt_child(GT.bodypanel, 0, &gslot0);
    for (int i = 0; i < gt_nchild(GT.sw); i++) { uintptr_t s; if (gt_child(GT.sw, i, &s) == GT.bodypanel) pslot0 = s; }
    if (!hslot0 || !txt0 || !bs0 || !grid0 || !gslot0 || !class_is(obj_class(grid0), "UniformGridPanel") || gt_nchild(grid0) < 5) { logf_("goal tab: the Body Size tab isn't built the way expected (%d%d%d%d%d, %d pictures), not added", !!hslot0, !!txt0, !!bs0, !!grid0, !!gslot0, grid0 ? gt_nchild(grid0) : 0); GT.fails = 9; DeleteFileW(GT.mark); return; }
    for (int i = 1; i <= GFX_N; i++) if (!gt_thumb(ctx, i)) { logf_("goal tab: picture %d not made", i); GT.fails++; DeleteFileW(GT.mark); return; }

    logf_("goal tab: pictures ready, creating the button");
    /* 1. the tab button, an exact copy of "Body Size" with other text */
    uintptr_t btn = gt_button_like(GT.tabbtn, tree, 0), txt = gt_spawn(obj_class(txt0), tree);
    if (!btn || !txt) { logf_("goal tab: the game didn't create the button"); GT.fails++; DeleteFileW(GT.mark); return; }
    gt_raw(txt, txt0, "Justification"); gt_raw(txt, txt0, "ShadowOffset"); gt_raw(txt, txt0, "ShadowColorAndOpacity"); gt_raw(txt, txt0, "MinDesiredWidth"); gt_raw(txt, txt0, "Visibility");
    gt_copy(txt, txt0, "Font", GT_TB_FONT); gt_copy(txt, txt0, "ColorAndOpacity", GT_TB_COL);
    logf_("goal tab: button created, setting text");
    gt_settext(txt, L"Goal Effect");
    logf_("goal tab: adding to the row");
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_ADDCHILD), &txt, 8); gt_call(btn, GT_ADDCHILD); uintptr_t bs = gt_ret(GT_ADDCHILD);
    if (bs) { gt_copy(bs, bs0, "Padding", GT_BS_PAD); gt_copy(bs, bs0, "HorizontalAlignment", GT_BS_HA); gt_copy(bs, bs0, "VerticalAlignment", GT_BS_VA); }
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_HB_ADD), &btn, 8); gt_call(GT.hb, GT_HB_ADD); uintptr_t hs = gt_ret(GT_HB_ADD);
    if (!hs) { logf_("goal tab: button not added to the row"); GT.fails++; DeleteFileW(GT.mark); return; }
    gt_copy(hs, hslot0, "Size", GT_HBS_SIZE); gt_copy(hs, hslot0, "Padding", GT_HBS_PAD); gt_copy(hs, hslot0, "HorizontalAlignment", GT_HBS_HA); gt_copy(hs, hslot0, "VerticalAlignment", GT_HBS_VA);

    logf_("goal tab: button in, creating the panel");
    /* 2. the panel + grid, copies of the Body Size ones */
    uintptr_t panel = gt_spawn(obj_class(GT.bodypanel), tree), grid = gt_spawn(obj_class(grid0), tree);
    if (!panel || !grid) { logf_("goal tab: the game didn't create the panel"); GT.fails++; DeleteFileW(GT.mark); return; }
    gt_raw(panel, GT.bodypanel, "Visibility"); gt_raw(grid, grid0, "Visibility"); gt_raw(grid, grid0, "SlotPadding"); gt_raw(grid, grid0, "MinDesiredSlotWidth"); gt_raw(grid, grid0, "MinDesiredSlotHeight");
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_ADDCHILD), &panel, 8); gt_call(GT.sw, GT_ADDCHILD); uintptr_t ps = gt_ret(GT_ADDCHILD);
    if (ps && pslot0) { gt_raw(ps, pslot0, "Padding"); gt_raw(ps, pslot0, "HorizontalAlignment"); gt_raw(ps, pslot0, "VerticalAlignment"); }
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_CV_ADD), &grid, 8); gt_call(panel, GT_CV_ADD); uintptr_t cs = gt_ret(GT_CV_ADD);
    if (cs) { gt_copy(cs, gslot0, "LayoutData", GT_CVS_LAYOUT); gt_copy(cs, gslot0, "ZOrder", GT_CVS_Z);
        uintptr_t ap = find_prop(obj_class(gslot0), "bAutoSize"); int32_t ao = -1; uint8_t bv = 0, mask = 1, v;
        if (prop_off(ap, &ao) && rd(gslot0 + ao, &bv, 1)) { rd(ap + 0x7A, &mask, 1); v = (bv & (mask ? mask : 1)) ? 1 : 0; gt_set1(cs, GT_CVS_AUTO, &v, 1); } }

    logf_("goal tab: panel in, creating the cells");
    /* 3. the picture buttons: same cells as the Body Size grid; the first five show the effects, the rest are the game's empty cells */
    uintptr_t pic0 = 0, empty0 = 0; int n = gt_nchild(grid0); if (n > GT_ITEMS) n = GT_ITEMS;
    for (int i = 0; i < n; i++) { uintptr_t c = gt_child(grid0, i, NULL); char nm[64] = ""; if (!c) continue; obj_name(c, nm, sizeof nm); if (!strncmp(nm, "Empty", 5)) { if (!empty0) empty0 = c; } else if (!pic0) pic0 = c; }
    if (!pic0) pic0 = gt_child(grid0, 0, NULL); if (!empty0) empty0 = pic0;
    GT.nitem = 0; int ro = -1, co = -1, fxn = 0;
    for (int i = 0; i < n; i++) {
        uintptr_t s0, c0 = gt_child(grid0, i, &s0); if (!c0 || !s0) continue;
        int id = fxn < GFX_N ? ++fxn : 0;
        uintptr_t b = gt_button_like(id ? pic0 : empty0, tree, id ? GT.tex[id - 1] : 0); if (!b) continue;
        if (ro < 0) { ro = gt_po(obj_class(s0), "Row"); co = gt_po(obj_class(s0), "Column"); }
        int32_t row = 0, col = 0; if (ro >= 0) rd(s0 + ro, &row, 4); if (co >= 0) rd(s0 + co, &col, 4);
        int a = gt_po(GTF[GT_UG_ADD].fn, "Content"), r1 = gt_po(GTF[GT_UG_ADD].fn, "InRow"), c1 = gt_po(GTF[GT_UG_ADD].fn, "InColumn"); if (a < 0 || r1 < 0 || c1 < 0) break;
        memset(GP, 0, 128); memcpy(GP + a, &b, 8); memcpy(GP + r1, &row, 4); memcpy(GP + c1, &col, 4); gt_call(grid, GT_UG_ADD); uintptr_t us = gt_ret(GT_UG_ADD);
        if (us) { gt_copy(us, s0, "HorizontalAlignment", GT_UGS_HA); gt_copy(us, s0, "VerticalAlignment", GT_UGS_VA); }
        GT.item[GT.nitem] = b; GT.fx[GT.nitem] = id; GT.st_item[GT.nitem] = 0; obj_id(b, &GT.ii[GT.nitem], &GT.is[GT.nitem]); GT.nitem++;
    }
    GT.mybtn = btn; GT.mypanel = panel; obj_id(btn, &GT.idx, &GT.ser); obj_id(panel, &GT.pi, &GT.ps); GT.st_tab = GT.st_save = GT.st_cancel = 0; GT.pending = 0;
    {   /* what the new button's text looks like next to the one it was copied from */
        wchar_t t1[48], t0[48]; char a1[48], a0[48]; int z; gt_readtext(txt, t1, 40); gt_readtext(txt0, t0, 40);
        for (z = 0; t1[z] && z < 47; z++) a1[z] = (char)t1[z]; a1[z] = 0; for (z = 0; t0[z] && z < 47; z++) a0[z] = (char)t0[z]; a0[z] = 0;
        int fo = gt_po(obj_class(txt0), "Font"), fz = gt_psz(obj_class(txt0), "Font"), co = gt_po(obj_class(txt0), "ColorAndOpacity"); uint8_t f1[160], f0[160]; float c1[4] = {0}, c0[4] = {0};
        int same = fo >= 0 && fz > 0 && fz <= 160 && rd(txt + fo, f1, fz) && rd(txt0 + fo, f0, fz) && !memcmp(f1, f0, fz); if (co >= 0) { rd(txt + co, c1, 16); rd(txt0 + co, c0, 16); }
        uintptr_t c = gt_child(btn, 0, NULL), sl = 0, par = 0; int so = gt_po(obj_class(txt), "Slot"); if (so >= 0) rdp(txt + so, &sl); if (sl) { int po2 = gt_po(obj_class(sl), "Parent"); if (po2 >= 0) rdp(sl + po2, &par); }
        logf_("goal tab: new text '%s' (copied from '%s'), font same %d (%d bytes), colour %.2f %.2f %.2f %.2f vs %.2f %.2f %.2f %.2f, inside button %d/%d", a1, a0, same, fz, c1[0], c1[1], c1[2], c1[3], c0[0], c0[1], c0[2], c0[3], c == txt, par == btn);
    }
    GT.built = 1; DeleteFileW(GT.mark);
    logf_("goal tab: added (button %p, panel %p, %d cells, %d effects)", (void *)btn, (void *)panel, GT.nitem, fxn);
}
static volatile LONG g_gs_post; static volatile int g_gs_unsynced;      /* unsynced: chosen here, the server doesn't have it yet */
static void gt_file(int id, int unsynced) { FILE *f = _wfopen(GT.file, L"w"); if (f) { fprintf(f, unsynced ? "%d\nu\n" : "%d\n", id); fclose(f); } }
static void gt_save(int id) {
    gt_file(id, 1);
    GT.saved = id; g_gfx_sel = id; g_gs_unsynced = 1; InterlockedExchange(&g_gs_post, id); logf_("goal tab: saved goal effect %d", id);
}
static void goaltab_tick(void *self) {                                                /* game thread, many times a second while a menu is open */
    static DWORD last, chk; DWORD now = GetTickCount();
    if (g_shutdown || now - last < 12 || !g_in_menu) return;
    last = now;
    if (GT.off) return;
    if (!GT.built) { if (GT.ready && GT.fns && self) gt_build(self); return; }
    (void)chk;
    {   /* every time, before touching anything: the menu may have just been torn down (joining a lobby). This runs on the game's own thread, so the answer can't change under us. */
        int ok = obj_alive3(GT.mybtn, GT.idx, GT.ser) && obj_alive3(GT.mypanel, GT.pi, GT.ps) && obj_alive3(GT.sw, GT.fi[2], GT.fs[2]) && obj_alive3(GT.save, GT.fi[4], GT.fs[4]) && (!GT.cancel || obj_alive3(GT.cancel, GT.fi[5], GT.fs[5]));
        for (int i = 0; ok && i < GT.nitem; i++) if (GT.fx[i] && !obj_alive3(GT.item[i], GT.ii[i], GT.is[i])) ok = 0;
        if (!ok) { GT.built = 0; GT.ready = 0; GT.stable = 0; memset(GT.prev, 0, sizeof GT.prev); GT.pending = 0; logf_("goal tab: the menu was closed or rebuilt, it will be added again"); return; }
    }
    if (gt_clicked(GT.mybtn, &GT.st_tab)) { gt_set1(GT.sw, GT_SW_SET, &GT.mypanel, 8); }
    uint8_t on = 1, offv = 0;
    for (int i = 0; i < GT.nitem; i++) if (GT.fx[i] && gt_clicked(GT.item[i], &GT.st_item[i])) {
        GT.pending = GT.fx[i]; InterlockedExchange(&g_gfx_req, GT.fx[i] | 0x200);
        gt_set1(GT.save, GT_ENABLE, &on, 1); if (GT.cancel) gt_set1(GT.cancel, GT_ENABLE, &on, 1);          /* like any other change: Save / Cancel Changes light up */
    }
    if (GT.save && gt_clicked(GT.save, &GT.st_save) && GT.pending) { gt_save(GT.pending); GT.pending = 0; gt_set1(GT.save, GT_ENABLE, &offv, 1); if (GT.cancel) gt_set1(GT.cancel, GT_ENABLE, &offv, 1); }
    if (GT.cancel && gt_clicked(GT.cancel, &GT.st_cancel) && GT.pending) { GT.pending = 0; gt_set1(GT.save, GT_ENABLE, &offv, 1); gt_set1(GT.cancel, GT_ENABLE, &offv, 1); }
}
static void goaltab_start(void) {
    wchar_t la[MAX_PATH]; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    swprintf(GT.file, MAX_PATH, L"%ls\\UE4PSO\\goalfx.txt", la);
    swprintf(GT.mark, MAX_PATH, L"%ls\\UE4PSO\\goaltab-building2.txt", la);
    if (GetFileAttributesW(GT.mark) != INVALID_FILE_ATTRIBUTES) { GT.off = 1; DeleteFileW(GT.mark); logf_("goal tab: it crashed the game last time, so it's switched off for this run"); }
    FILE *f = _wfopen(GT.file, L"r"); int id = 0; char u[8] = "";
    if (f) { if (fscanf(f, "%d %7s", &id, u) >= 1 && id >= 1 && id <= GFX_N) { GT.saved = id; g_gfx_sel = id;
            if (u[0] == 'u') { g_gs_unsynced = 1; InterlockedExchange(&g_gs_post, id); } }      /* saved here last time but never reached the server: send it again */
        fclose(f); }
}

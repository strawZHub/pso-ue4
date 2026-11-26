/* goalvid.h - "Goal Effects  < On >" row at the bottom of the game's own Settings > Video list (main menu and in a match).
 * Nothing is drawn by us: the row is put together by the game from copies of the parts of one of its own On/Off rows
 * (background, name text, value text, the two arrow buttons and arrow pictures), with the same sizes and positions.
 * Clicking either arrow switches between On and Off; the choice is kept in %LOCALAPPDATA%\UE4PSO\goalfx-on.txt.
 * Off = this player neither sees nor hears goal effects (anybody's).
 * Hover: the game changes a row's colours while the mouse is on its arrows. The first time that happens on one of the
 * game's own rows, what the game did to it is noted, and our row then does the same when the mouse is on its arrows.
 * Widgets are looked for on the effects thread; everything is created and changed on the game thread. */
typedef struct { const char *outer, *name; uintptr_t fn; } GvFn;
enum { GV_IMG_BRUSH, GV_IMG_COL, GV_SBS_PAD, GV_SBS_HA, GV_SZ_W, GV_SZ_H, GV_DESIRED, GV_BTN_BG, GV_BTN_COL, GV_OPAC, GV_N };
static GvFn GVF[GV_N] = { {"Image", "SetBrush"}, {"Image", "SetColorAndOpacity"}, {"ScrollBoxSlot", "SetPadding"}, {"ScrollBoxSlot", "SetHorizontalAlignment"},
    {"SizeBox", "SetWidthOverride"}, {"SizeBox", "SetHeightOverride"}, {"Widget", "GetDesiredSize"}, {"Button", "SetBackgroundColor"}, {"Button", "SetColorAndOpacity"}, {"Widget", "SetRenderOpacity"} };
typedef struct { uintptr_t item, root, bg, sw, lr, left, right, val, name, la, ra, s_bg, s_sw; } GvRow;
typedef struct { int ok, bsz, csz, bok; uint8_t bgbrush[200], labrush[200], rabrush[200], ncol[48], vcol[48]; float bgcol[4], lacol[4], racol[4], lb[4], lc[4], rb[4], rc[4]; } GvLook;
typedef struct { int ok; float bg[4], col[4], ro; } GvRef;             /* how an arrow button is set up: background colour, colour, opacity */
#define GV_ROWS 16
static struct {
    volatile LONG ready, built, fns; int off, fails;
    uintptr_t found, scroll, sizebox_cls; int32_t si, ss;
    uintptr_t box, left, right, val, name, bg, la, ra, item0, islot0; int32_t bi, bs, li, ls, ri, rs, vi, vs, i0i, i0s; int st_l, st_r, hov, fast, cand, polls, fixed, menu;
    GvRow rows[GV_ROWS]; int nrows; GvLook lnorm, lhov; GvRef ref;
    wchar_t file[MAX_PATH], mark[MAX_PATH], reffile[MAX_PATH]; DWORD last_try; int waitlog;
    uintptr_t ksl_cdo, fn_exec; int gc_set; wchar_t hovfile[MAX_PATH]; DWORD built_at, dark_t, hov_t; int saw_hover, saw_press, dark_at, diag[2];                          /* the engine's own "run a console command" (for the memory clean-up interval) */
} GV;
static void gv_save(void) { FILE *f = _wfopen(GV.file, L"w"); if (f) { fprintf(f, "%d\n", (int)g_gfx_enabled); fclose(f); } }
static const wchar_t *gv_word(void) { return g_gfx_enabled == 1 ? L"On" : g_gfx_enabled == 2 ? L"No Sound" : L"Off"; }

/* ---- small helpers (game thread; same parameter buffer as the Goal Effect tab) */
static int gv_first(uintptr_t fn) { uintptr_t p = 0; int32_t o = 0; if (rdp(fn + 0x50, &p) && p) rd(p + 0x4C, &o, 4); return o; }
static void gv_call(uintptr_t obj, uintptr_t fn) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)obj, (void *)fn, GP); }
static int gv_copy(uintptr_t dst, uintptr_t src, const char *prop, uintptr_t fn) {                 /* dst.Setter(src.prop) */
    int off = gt_po(obj_class(src), prop), sz = gt_psz(obj_class(src), prop), po = gv_first(fn);
    if (!fn || off < 0 || sz <= 0 || po + sz > 3900) return 0;
    memset(GP, 0, po + sz + 64); if (!rd(src + off, GP + po, sz)) return 0;
    gv_call(dst, fn); return 1;
}
static void gv_set(uintptr_t o, uintptr_t fn, const void *v, int n) { int po = gv_first(fn); if (!o || !fn || po + n > 3900) return; memset(GP, 0, po + n + 64); memcpy(GP + po, v, n); gv_call(o, fn); }
static void gv_set_quiet(uintptr_t o, uintptr_t fn, const void *v, int n) { g_in_wrap = 1; gv_set(o, fn, v, n); g_in_wrap = 0; }      /* values that are already final: the day/night theme leaves them alone */
static uintptr_t gv_named(uintptr_t panel, const char *want, uintptr_t *slot) {                    /* a panel's child by name */
    for (int i = 0; i < gt_nchild(panel); i++) { uintptr_t s = 0, c = gt_child(panel, i, &s); char n[64]; if (c && obj_name(c, n, sizeof n) && !strcmp(n, want)) { if (slot) *slot = s; return c; } }
    if (slot) *slot = 0; return 0;
}
static uintptr_t gv_slot_parent(uintptr_t w) { uintptr_t slot = 0, par = 0; int so = gt_po(obj_class(w), "Slot"); if (so < 0 || !rdp(w + so, &slot) || !slot) return 0; int po = gt_po(obj_class(slot), "Parent"); if (po >= 0) rdp(slot + po, &par); return par; }
static uintptr_t gv_canvas_add(uintptr_t canvas, uintptr_t child, uintptr_t s0) {                   /* into a canvas, at the same place / size as slot s0 */
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_CV_ADD), &child, 8); gt_call(canvas, GT_CV_ADD); uintptr_t cs = gt_ret(GT_CV_ADD);
    if (cs && s0 && class_is(obj_class(s0), "CanvasPanelSlot")) { gt_copy(cs, s0, "LayoutData", GT_CVS_LAYOUT); gt_copy(cs, s0, "ZOrder", GT_CVS_Z);
        uintptr_t ap = find_prop(obj_class(s0), "bAutoSize"); int32_t ao = -1; uint8_t bv = 0, mask = 1, v;
        if (prop_off(ap, &ao) && rd(s0 + ao, &bv, 1)) { rd(ap + 0x7A, &mask, 1); v = (bv & (mask ? mask : 1)) ? 1 : 0; gt_set1(cs, GT_CVS_AUTO, &v, 1); } }
    return cs;
}
static uintptr_t gv_image_like(uintptr_t src, uintptr_t tree) {
    uintptr_t img = gt_spawn(obj_class(src), tree); if (!img) return 0;
    gt_raw(img, src, "Visibility"); gv_copy(img, src, "Brush", GVF[GV_IMG_BRUSH].fn); gv_copy(img, src, "ColorAndOpacity", GVF[GV_IMG_COL].fn);
    return img;
}
static uintptr_t gv_text_like(uintptr_t src, uintptr_t tree, const wchar_t *w) {
    uintptr_t t = gt_spawn(obj_class(src), tree); if (!t) return 0;
    gt_raw(t, src, "Justification"); gt_raw(t, src, "ShadowOffset"); gt_raw(t, src, "ShadowColorAndOpacity"); gt_raw(t, src, "MinDesiredWidth"); gt_raw(t, src, "Visibility");
    gt_copy(t, src, "Font", GT_TB_FONT); gt_copy(t, src, "ColorAndOpacity", GT_TB_COL);
    gt_settext(t, w);
    return t;
}
/* one of the game's rows: its parts (reads only). full = also the pictures and texts, not just the two arrow buttons */
static int gv_row(uintptr_t item, GvRow *r, int full) {
    memset(r, 0, sizeof *r); r->item = item;
    int wo = gt_po(obj_class(item), "WidgetTree"); uintptr_t wt = 0; if (wo < 0 || !rdp(item + wo, &wt) || !wt) return 0;
    int rw = gt_po(obj_class(wt), "RootWidget"); if (rw < 0 || !rdp(wt + rw, &r->root) || !r->root) return 0;
    r->sw = gv_named(r->root, "ItemTypeSwitcher", &r->s_sw); if (!r->sw) return 0;
    r->lr = gv_named(r->sw, "LeftRightSelectionItem", NULL); if (!r->lr) return 0;
    for (int i = 0; i < gt_nchild(r->lr); i++) { uintptr_t c = gt_child(r->lr, i, NULL); char n[64]; if (!c || !obj_name(c, n, sizeof n)) continue;
        if (!strcmp(n, "LeftButton")) r->left = c; else if (!strcmp(n, "RightButton")) r->right = c;
        else if (!strcmp(n, "LeftRightSelectionText")) r->val = c; else if (!strcmp(n, "LeftRightSelectionName")) r->name = c;
        else if (!strcmp(n, "LeftArrow")) r->la = c; else if (!strcmp(n, "RightArrow")) r->ra = c; }
    if (full) r->bg = gv_named(r->root, "BackgroundImage", &r->s_bg);
    return r->left && r->right;
}
static int gv_hovered(uintptr_t btn) { return btn ? gt_bool(btn, GT_HOVER) : 0; }
/* what a row looks like right now (its colours and pictures) */
static int gv_sample(const GvRow *r, GvLook *L) {
    memset(L, 0, sizeof *L);
    if (!r->name || !r->val) return 0;
    uintptr_t ic = r->bg ? obj_class(r->bg) : r->la ? obj_class(r->la) : 0, tc = obj_class(r->name);
    L->csz = gt_psz(tc, "ColorAndOpacity"); int co = gt_po(tc, "ColorAndOpacity"); if (L->csz < 16 || L->csz > 48 || co < 0) return 0;
    if (!rd(r->name + co, L->ncol, L->csz) || !rd(r->val + co, L->vcol, L->csz)) return 0;
    if (L->csz >= 40) { memset(L->ncol + 24, 0, 16); memset(L->vcol + 24, 0, 16); }                    /* a colour's link to another colour object: not carried over */
    if (ic) { int bo = gt_po(ic, "Brush"), io = gt_po(ic, "ColorAndOpacity"); L->bsz = gt_psz(ic, "Brush");
        if (bo < 0 || io < 0 || L->bsz < 32 || L->bsz > 200 || g_rh_off < 0 || g_rh_off + 16 > L->bsz) L->bsz = 0;
        if (r->bg && io >= 0) rd(r->bg + io, L->bgcol, 16); if (r->la && io >= 0) rd(r->la + io, L->lacol, 16); if (r->ra && io >= 0) rd(r->ra + io, L->racol, 16);
        if (L->bsz) { if (r->bg) rd(r->bg + bo, L->bgbrush, L->bsz); if (r->la) rd(r->la + bo, L->labrush, L->bsz); if (r->ra) rd(r->ra + bo, L->rabrush, L->bsz);
            memset(L->bgbrush + g_rh_off, 0, 16); memset(L->labrush + g_rh_off, 0, 16); memset(L->rabrush + g_rh_off, 0, 16); } }      /* the game looks the picture up again by itself */
    { uintptr_t bc = obj_class(r->left); int b = gt_po(bc, "BackgroundColor"), k = gt_po(bc, "ColorAndOpacity");
      L->bok = b >= 0 && k >= 0 && rd(r->left + b, L->lb, 16) && rd(r->left + k, L->lc, 16) && rd(r->right + b, L->rb, 16) && rd(r->right + k, L->rc, 16); }
    L->ok = 1; return 1;
}
/* The hover look is learned by watching one of the game's rows under the mouse. Its colours are kept in a file (one
 * line for Sun, one for Moon) so the next time the row shows the right hover look at once; pictures aren't kept. */
static int gv_brush_same(const uint8_t *a, const uint8_t *b, int n) { return n <= 0 || !memcmp(a, b, n); }
static void gv_hover_save(const GvLook *L) {
    if (!L->bok || !gv_brush_same(L->bgbrush, GV.lnorm.bgbrush, L->bsz)) return;                   /* a different background picture on hover: colours alone wouldn't look right */
    float v[2][36]; int have[2] = { 0, 0 }; FILE *f = _wfopen(GV.hovfile, L"r");
    if (f) { char line[1200]; while (fgets(line, sizeof line, f)) { int t = line[0] == 'M' ? 1 : line[0] == 'S' ? 0 : -1; if (t < 0) continue; char *q = line + 1; int n = 0; while (n < 36) { char *e; float x = strtof(q, &e); if (e == q) break; v[t][n++] = x; q = e; } have[t] = n == 36; } fclose(f); }
    int t = g_dark ? 1 : 0; float *o = v[t]; have[t] = 1;
    memcpy(o, L->ncol, 16); memcpy(o + 4, L->vcol, 16); memcpy(o + 8, L->bgcol, 16); memcpy(o + 12, L->lacol, 16); memcpy(o + 16, L->racol, 16);
    memcpy(o + 20, L->lb, 16); memcpy(o + 24, L->lc, 16); memcpy(o + 28, L->rb, 16); memcpy(o + 32, L->rc, 16);
    f = _wfopen(GV.hovfile, L"w"); if (!f) return;
    for (int k = 0; k < 2; k++) if (have[k]) { fputc(k ? 'M' : 'S', f); for (int i = 0; i < 36; i++) fprintf(f, " %.5f", v[k][i]); fputc('\n', f); }
    fclose(f);
}
static void gv_hover_load(void) {                                                                   /* after the normal look is known */
    if (!GV.lnorm.ok || !GV.lnorm.bok) return;
    FILE *f = _wfopen(GV.hovfile, L"r"); if (!f) return;
    char line[1200]; float v[36]; int got = 0;
    while (!got && fgets(line, sizeof line, f)) { if (line[0] != (g_dark ? 'M' : 'S')) continue; char *q = line + 1; int n = 0; while (n < 36) { char *e; float x = strtof(q, &e); if (e == q) break; if (!(x >= 0 && x <= 4)) { n = 0; break; } v[n++] = x; q = e; } got = n == 36; }
    fclose(f); if (!got) return;
    GvLook L = GV.lnorm; L.bsz = 0;                                                                 /* same pictures as now, the saved colours */
    memcpy(L.ncol, v, 16); memcpy(L.vcol, v + 4, 16); memcpy(L.bgcol, v + 8, 16); memcpy(L.lacol, v + 12, 16); memcpy(L.racol, v + 16, 16);
    memcpy(L.lb, v + 20, 16); memcpy(L.lc, v + 24, 16); memcpy(L.rb, v + 28, 16); memcpy(L.rc, v + 32, 16);
    L.ok = 2; GV.lhov = L; logf_("goal setting: hover colours taken from last time (%s)", g_dark ? "Moon" : "Sun");
}
static const char *gv_cursor_name(void) {
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci; if (!GetCursorInfo(&ci)) return "unknown";
    if (!(ci.flags & CURSOR_SHOWING)) return "hidden";
    if (ci.hCursor == LoadCursor(NULL, IDC_ARROW)) return "arrow"; if (ci.hCursor == LoadCursor(NULL, IDC_APPSTARTING)) return "arrow with busy wheel";
    if (ci.hCursor == LoadCursor(NULL, IDC_WAIT)) return "busy wheel"; if (ci.hCursor == LoadCursor(NULL, IDC_HAND)) return "hand"; return "the game's own";
}
static void gv_apply(const GvLook *L) {                                                             /* our row takes that look */
    if (!L->ok) return;
    if (GV.name) gv_set_quiet(GV.name, GTF[GT_TB_COL].fn, L->ncol, L->csz);
    if (GV.val) gv_set_quiet(GV.val, GTF[GT_TB_COL].fn, L->vcol, L->csz);
    if (GV.bg) { if (L->bsz) gv_set_quiet(GV.bg, GVF[GV_IMG_BRUSH].fn, L->bgbrush, L->bsz); gv_set_quiet(GV.bg, GVF[GV_IMG_COL].fn, L->bgcol, 16); }
    if (GV.la) { if (L->bsz) gv_set_quiet(GV.la, GVF[GV_IMG_BRUSH].fn, L->labrush, L->bsz); gv_set_quiet(GV.la, GVF[GV_IMG_COL].fn, L->lacol, 16); }
    if (GV.ra) { if (L->bsz) gv_set_quiet(GV.ra, GVF[GV_IMG_BRUSH].fn, L->rabrush, L->bsz); gv_set_quiet(GV.ra, GVF[GV_IMG_COL].fn, L->racol, 16); }
    if (L->bok) { gv_set_quiet(GV.left, GVF[GV_BTN_BG].fn, L->lb, 16); gv_set_quiet(GV.left, GVF[GV_BTN_COL].fn, L->lc, 16); gv_set_quiet(GV.right, GVF[GV_BTN_BG].fn, L->rb, 16); gv_set_quiet(GV.right, GVF[GV_BTN_COL].fn, L->rc, 16); }
}
/* how one arrow button is set up; also written to the log (why the in-match Settings can look different from the main menu's) */
static int gv_btn_facts(uintptr_t btn, GvRef *o, const char *where) {
    uintptr_t c = obj_class(btn); int b = gt_po(c, "BackgroundColor"), k = gt_po(c, "ColorAndOpacity"), ro = gt_po(c, "RenderOpacity"), so = gt_po(c, "WidgetStyle");
    memset(o, 0, sizeof *o); o->ro = 1;
    if (b < 0 || k < 0 || !rd(btn + b, o->bg, 16) || !rd(btn + k, o->col, 16)) return 0;
    if (ro >= 0) rd(btn + ro, &o->ro, 4);
    o->ok = 1;
    if (where) { uintptr_t sp = find_prop(c, "WidgetStyle"), st = 0, np, bst = 0; int draw = -1; float tint[4] = { -1, -1, -1, -1 }; char res[64] = "-";
        if (so >= 0 && sp && rdp(sp + 0x78, &st) && st && (np = find_prop(st, "Normal")) && rdp(np + 0x78, &bst) && bst) { int no = gt_po(st, "Normal"), d = gt_po(bst, "DrawAs"), t = gt_po(bst, "TintColor"), r = gt_po(bst, "ResourceObject"); uint8_t dv = 255; uintptr_t rob = 0;
            if (no >= 0 && d >= 0 && rd(btn + so + no + d, &dv, 1)) draw = dv; if (no >= 0 && t >= 0) rd(btn + so + no + t, tint, 16); if (no >= 0 && r >= 0 && rdp(btn + so + no + r, &rob) && rob) obj_name(rob, res, sizeof res); }
        logf_("goal setting: a game arrow button %s: background %.2f %.2f %.2f %.2f, colour %.2f %.2f %.2f %.2f, opacity %.2f; its normal look: draw type %d, tint %.2f %.2f %.2f %.2f, picture %s",
              where, o->bg[0], o->bg[1], o->bg[2], o->bg[3], o->col[0], o->col[1], o->col[2], o->col[3], o->ro, draw, tint[0], tint[1], tint[2], tint[3], res); }
    return 1;
}
static void gv_ref_file(int save) {
    FILE *f = _wfopen(GV.reffile, save ? L"w" : L"r"); if (!f) return;
    GvRef *r = &GV.ref;
    if (save) fprintf(f, "%f %f %f %f %f %f %f %f %f\n", r->bg[0], r->bg[1], r->bg[2], r->bg[3], r->col[0], r->col[1], r->col[2], r->col[3], r->ro);
    else if (fscanf(f, "%f %f %f %f %f %f %f %f %f", &r->bg[0], &r->bg[1], &r->bg[2], &r->bg[3], &r->col[0], &r->col[1], &r->col[2], &r->col[3], &r->ro) == 9) r->ok = 1;
    fclose(f);
}
/* In a match the game's own arrow buttons can come out as plain grey boxes that cover the values. If they are set up
 * differently from the main menu's (noted there, kept in a small file), every row in every tab gets the main-menu set-up. */
static void gv_fix_match_rows(uintptr_t sc, int pass) {              /* pass 1: when the match's Settings is found; pass 2: when its Video tab is first shown */
    if (GV.fixed >= pass) return;
    GV.fixed = pass;
    if (!GV.ref.ok) { if (pass == 1) logf_("goal setting: in-match Settings found; no main-menu note yet to compare its arrow buttons with (open Settings > Video in the main menu once)"); return; }
    uintptr_t tab = gv_slot_parent(sc), tabs = tab ? gv_slot_parent(tab) : 0; int n = 0, seen = 0; GvRow r; GvRef here;
    if (!tabs || !class_is(obj_class(tabs), "WidgetSwitcher")) return;
    for (int t = 0; t < gt_nchild(tabs) && t < 8; t++) { uintptr_t tc = gt_child(tabs, t, NULL), list = 0; if (!tc) continue;
        for (int i = 0; i < gt_nchild(tc) && !list; i++) { uintptr_t c = gt_child(tc, i, NULL); if (c && class_is(obj_class(c), "ScrollBox")) list = c; }
        for (int i = 0; list && i < gt_nchild(list) && i < 64; i++) { uintptr_t it = gt_child(list, i, NULL); if (!it || !class_is(obj_class(it), "SettingsListItem_C") || !gv_row(it, &r, 0)) continue;
            uintptr_t b2[2] = { r.left, r.right }; if (!seen++ && pass == 1) gv_btn_facts(r.left, &here, "(in a match, as the game made it)");
            for (int k = 0; k < 2; k++) { if (!gv_btn_facts(b2[k], &here, NULL)) continue;
                if (fabsf(here.bg[3] - GV.ref.bg[3]) > 0.3f || fabsf(here.col[3] - GV.ref.col[3]) > 0.3f) { gv_set_quiet(b2[k], GVF[GV_BTN_BG].fn, GV.ref.bg, 16); gv_set_quiet(b2[k], GVF[GV_BTN_COL].fn, GV.ref.col, 16); n++; }
                if (fabsf(here.ro - GV.ref.ro) > 0.3f) { gv_set_quiet(b2[k], GVF[GV_OPAC].fn, &GV.ref.ro, 4); n++; } } } }
    if (n || pass == 1) logf_("goal setting: in-match Settings (look %d): %d rows, %d arrow-button settings put right (main menu has background alpha %.2f, colour alpha %.2f, opacity %.2f)", pass, seen, n, GV.ref.bg[3], GV.ref.col[3], GV.ref.ro);
}

/* ---- finding the Video list (effects thread, until the row is in) */
static int gv_find_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; char nm[64], on[64];
    if (!obj_name(o, nm, sizeof nm)) return 0;
    if (!GV.fns) {
        static uintptr_t fcls; if (!fcls && class_is(cls, "Function")) fcls = cls;
        if (cls == fcls) { uintptr_t ou = 0; if (rdp(o + 0x20, &ou) && obj_name(ou, on, sizeof on)) { for (int i = 0; i < GV_N; i++) if (!GVF[i].fn && !strcmp(nm, GVF[i].name) && !strcmp(on, GVF[i].outer)) GVF[i].fn = o;
                if (!GV.fn_exec && !strcmp(nm, "ExecuteConsoleCommand") && !strcmp(on, "KismetSystemLibrary")) GV.fn_exec = o; } return 0; }
        if (!GV.sizebox_cls && !strcmp(nm, "SizeBox") && class_is(cls, "Class")) { GV.sizebox_cls = o; return 0; }
        if (!GV.ksl_cdo && !strcmp(nm, "Default__KismetSystemLibrary")) { GV.ksl_cdo = o; return 0; }
    }
    if (strcmp(nm, "ScrollBox")) return 0;
    uintptr_t tree = 0, uw = 0, up = 0, par;                                                       /* the Video tab's list in a live Settings screen, not in its blueprint */
    if (!rdp(o + 0x20, &tree) || !tree || !rdp(tree + 0x20, &uw) || !uw || !class_is(obj_class(uw), "Settings_C")) return 0;
    if (!rdp(uw + 0x20, &up) || !up || !class_is(obj_class(up), "WidgetTree")) return 0;
    par = gv_slot_parent(o); if (!par || !obj_name(par, on, sizeof on) || strcmp(on, "VideoTab")) return 0;
    GV.found = o; return 0;
}
static void goalvid_scan(void) {
    static int was_menu = -1, tries; int menu = g_in_menu;
    if (menu != was_menu) { was_menu = menu; tries = 0; }
    if (GV.off || GV.built || GV.ready || GV.fails > 3 || !GT.fns || !g_logged_in) return;
    if (!menu && tries >= 8) return;                                                               /* in a match: only a few looks, right after it starts */
    tries++;
    GV.found = 0; for_each_object(gv_find_cb, NULL);
    if (!GV.fns) { int ok = GV.sizebox_cls != 0; for (int i = 0; i < GV_N; i++) if (!GVF[i].fn) { ok = 0; static int lg; if (lg++ < 14) logf_("goal setting: game function %s.%s not found", GVF[i].outer, GVF[i].name); }
        if (ok) GV.fns = 1; else { GV.fails++; return; } }
    if (!GV.found) return;
    GV.scroll = GV.found; obj_id(GV.scroll, &GV.si, &GV.ss); GV.fixed = 0; GV.waitlog = 0; GV.item0 = 0; GV.ready = 1;
}

/* ---- building the row (game thread) */
static void gv_build(void) {
    uintptr_t sc = GV.scroll, tree = 0, item0, islot0; char n[64]; int menu = GV.menu;
    if (!obj_alive3(sc, GV.si, GV.ss)) { GV.ready = 0; return; }
    if (!GV.item0) {                                                                       /* once per Settings screen: the list's first row, and that our row isn't in it already */
        if (!obj_name(sc, n, sizeof n) || strcmp(n, "ScrollBox")) { GV.ready = 0; return; }
        int nc0 = gt_nchild(sc); uintptr_t it = 0, sl = 0;
        for (int i = 0; i < nc0 && !it; i++) { uintptr_t s2 = 0, c = gt_child(sc, i, &s2); if (c && s2 && class_is(obj_class(c), "SettingsListItem_C")) { it = c; sl = s2; } }
        for (int i = 0; i < nc0; i++) { uintptr_t c = gt_child(sc, i, NULL); if (c && obj_class(c) == GV.sizebox_cls) { logf_("goal setting: already there"); GV.fails = 9; GV.ready = 0; return; } }
        if (!it) { logf_("goal setting: the Video list has no rows to copy, not added"); GV.fails = 9; GV.ready = 0; return; }
        GV.item0 = it; GV.islot0 = sl; obj_id(it, &GV.i0i, &GV.i0s);
        { uintptr_t o = sc; GV.menu = 0; for (int d = 0; d < 10 && o && !GV.menu; d++) { if (class_is(obj_class(o), "MainMenu_C")) GV.menu = 1; if (!rdp(o + 0x20, &o)) break; } menu = GV.menu; }      /* which Settings screen this is: the main menu's or a match's */
        if (!menu) gv_fix_match_rows(sc, 1);
    }
    item0 = GV.item0; islot0 = GV.islot0;
    if (!obj_alive3(item0, GV.i0i, GV.i0s) || !rdp(sc + 0x20, &tree) || !tree) { GV.ready = 0; return; }
    /* the game works out a row's size the first time the Video tab is shown: wait for that (one cheap question per look) */
    float sz[2] = { 0, 0 }; int ro = gt_po(GVF[GV_DESIRED].fn, "ReturnValue"); if (ro < 0) { GV.fails = 9; GV.ready = 0; return; }
    memset(GP, 0, 64); gv_call(item0, GVF[GV_DESIRED].fn); memcpy(sz, GP + ro, 8);
    if (!(sz[0] > 40 && sz[0] < 6000 && sz[1] > 8 && sz[1] < 600)) { if (!GV.waitlog++) logf_("goal setting: Video list found (%s), waiting for the Video tab to be opened", menu ? "main menu" : "in a match"); return; }
    int nc = gt_nchild(sc);

    /* the game's rows in this list; the first one that is an arrows row and isn't under the mouse is the one copied */
    GvRow src; int have = 0; GV.nrows = 0;
    for (int i = 0; i < nc && GV.nrows < GV_ROWS; i++) { uintptr_t c = gt_child(sc, i, NULL); if (!c || !class_is(obj_class(c), "SettingsListItem_C")) continue;
        GvRow *r = &GV.rows[GV.nrows]; if (!gv_row(c, r, 1) || !r->val || !r->name) continue; GV.nrows++;
        if (!have && !gv_hovered(r->item)) { src = *r; have = 1; } }
    if (!have || !class_is(obj_class(src.root), "CanvasPanel") || !class_is(obj_class(src.lr), "CanvasPanel") || !class_is(obj_class(src.left), "Button") || !class_is(obj_class(src.val), "TextBlock") || !src.s_sw) {
        logf_("goal setting: a settings row isn't built the way expected (%d rows usable), not added", GV.nrows); GV.fails = 9; GV.ready = 0; return; }
    { GvRef here; if (gv_btn_facts(src.left, &here, menu ? "(main menu)" : "(in a match)") && menu) { GV.ref = here; gv_ref_file(1); } }
    if (!menu) gv_fix_match_rows(sc, 2);

    { FILE *mf = _wfopen(GV.mark, L"w"); if (mf) { fputs("building\n", mf); fclose(mf); } }             /* removed again when the row is in; left behind = it crashed */
    logf_("goal setting: building (row size %.0f x %.0f, %s)", sz[0], sz[1], menu ? "main menu" : "in a match");
    uintptr_t box = gt_spawn(GV.sizebox_cls, tree), row = gt_spawn(obj_class(src.root), tree), inner = gt_spawn(obj_class(src.lr), tree);
    if (!box || !row || !inner) { logf_("goal setting: the game didn't create the row"); GV.fails++; GV.ready = 0; DeleteFileW(GV.mark); return; }
    gt_raw(row, src.root, "Visibility"); gt_raw(inner, src.lr, "Visibility");
    gv_set(box, GVF[GV_SZ_W].fn, &sz[0], 4); gv_set(box, GVF[GV_SZ_H].fn, &sz[1], 4);
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_ADDCHILD), &row, 8); gt_call(box, GT_ADDCHILD);
    GV.bg = GV.la = GV.ra = GV.name = 0;
    if (src.bg && class_is(obj_class(src.bg), "Image")) { GV.bg = gv_image_like(src.bg, tree); if (GV.bg) gv_canvas_add(row, GV.bg, src.s_bg); }
    gv_canvas_add(row, inner, src.s_sw);
    /* the parts, added in the same order as in the game's row so they stack the same way */
    uintptr_t left = 0, right = 0, val = 0;
    for (int i = 0; i < gt_nchild(src.lr); i++) { uintptr_t s0 = 0, c0 = gt_child(src.lr, i, &s0), c = 0; if (!c0) continue;
        if (c0 == src.left) c = left = gt_button_like(src.left, tree, 0);
        else if (c0 == src.right) c = right = gt_button_like(src.right, tree, 0);
        else if (c0 == src.val) c = val = gv_text_like(src.val, tree, gv_word());
        else if (c0 == src.name) c = GV.name = gv_text_like(src.name, tree, L"Goal Effects");
        else if (c0 == src.la && class_is(obj_class(c0), "Image")) c = GV.la = gv_image_like(c0, tree);
        else if (c0 == src.ra && class_is(obj_class(c0), "Image")) c = GV.ra = gv_image_like(c0, tree);
        if (c) gv_canvas_add(inner, c, s0); }
    if (!left || !right || !val) { logf_("goal setting: the game didn't create the arrows / text"); GV.fails++; GV.ready = 0; DeleteFileW(GV.mark); return; }
    memset(GP, 0, 64); memcpy(GP + gt_first(GT_ADDCHILD), &box, 8); gt_call(sc, GT_ADDCHILD); uintptr_t ss = gt_ret(GT_ADDCHILD);
    if (!ss) { logf_("goal setting: row not added to the list"); GV.fails++; GV.ready = 0; DeleteFileW(GV.mark); return; }
    gv_copy(ss, islot0, "Padding", GVF[GV_SBS_PAD].fn); gv_copy(ss, islot0, "HorizontalAlignment", GVF[GV_SBS_HA].fn);
    GV.box = box; GV.left = left; GV.right = right; GV.val = val; GV.st_l = GV.st_r = 0; GV.hov = 0; GV.fast = 0; GV.cand = -1; GV.polls = 0;
    obj_id(box, &GV.bi, &GV.bs); obj_id(left, &GV.li, &GV.ls); obj_id(right, &GV.ri, &GV.rs); obj_id(val, &GV.vi, &GV.vs);
    gv_sample(&src, &GV.lnorm); memset(&GV.lhov, 0, sizeof GV.lhov); gv_hover_load(); GV.built_at = GetTickCount(); GV.saw_hover = GV.saw_press = 0; GV.dark_at = g_dark; GV.dark_t = 0;
    { uintptr_t mine[10] = { box, row, inner, GV.bg, GV.la, GV.ra, GV.name, val, left, right }; for (int i = 0; i < 10; i++) theme_leave_alone(mine[i]); }      /* the row copies the game's rows as they look now, day or night */
    gv_apply(&GV.lnorm);
    GV.built = 1; DeleteFileW(GV.mark);
    logf_("goal setting: added to Settings > Video, %s (goal effects setting %d)", menu ? "main menu" : "in a match", (int)g_gfx_enabled);
}
/* The engine tidies its memory every 61 s and the game stops for one long frame each time. The interval is also
 * written into the game's settings file when the game starts (see gc_interval_setup); this sets it for the running
 * game as well, once, through the engine's own console-command function, in case the file isn't picked up. */
static void gv_gc_interval(void) {
    static wchar_t cmd[] = L"gc.TimeBetweenPurgingPendingKillObjects 900";
    if (GV.gc_set || !GV.fn_exec || !GV.ksl_cdo || !GV.scroll || !obj_alive3(GV.scroll, GV.si, GV.ss)) return;
    GV.gc_set = 1;
    int a = gt_po(GV.fn_exec, "WorldContextObject"), b = gt_po(GV.fn_exec, "Command"); if (a < 0 || b < 0 || a > 64 || b > 64) { logf_("smoothness: the engine's command function looks different, interval left to the settings file"); return; }
    UStr s = ustr(cmd); memset(GP, 0, 128); memcpy(GP + a, &GV.scroll, 8); memcpy(GP + b, &s, 16);
    gv_call(GV.ksl_cdo, GV.fn_exec);
    logf_("smoothness: asked the engine to tidy its memory every 15 minutes instead of every 61 seconds (each tidy-up stops the game for one long frame)");
}
static void goalvid_tick(void) {                                                          /* game thread */
    static DWORD last; DWORD now = GetTickCount();
    if (g_shutdown || GV.off || now - last < (DWORD)(GV.built && (GV.fast || now - GV.built_at < 20000) ? 12 : 60)) return;   /* also for the first seconds after the row appears */   /* often only while the mouse is on our arrows */
    last = now;
    if (GV.ready && !GV.gc_set) gv_gc_interval();
    if (!GV.built) { if (GV.ready && GV.fns && GT.fns && now - GV.last_try > 90) { GV.last_try = now; gv_build(); } return; }
    if (!(obj_alive3(GV.box, GV.bi, GV.bs) && obj_alive3(GV.left, GV.li, GV.ls) && obj_alive3(GV.right, GV.ri, GV.rs) && obj_alive3(GV.val, GV.vi, GV.vs) && obj_alive3(GV.scroll, GV.si, GV.ss))) {
        GV.built = 0; GV.ready = 0; GV.waitlog = 0; GV.nrows = 0; GV.lhov.ok = GV.lnorm.ok = 0; return; }      /* that Settings screen is gone (menu <-> match): it is added to the new one */
    int hov = gv_hovered(GV.box) || gv_hovered(GV.left) || gv_hovered(GV.right) || (GV.bg && gv_hovered(GV.bg));      /* anywhere on the row, like the game's rows */
    GV.fast = hov || GV.st_l || GV.st_r;
    if (GV.dark_at != g_dark) {                                                                   /* Sun <-> Moon was switched: take the game rows' new look once the theme has finished */
        if (!GV.dark_t) GV.dark_t = now;
        else if (now - GV.dark_t > 700) { int done = 0;
            for (int i = 0; i < GV.nrows && !done; i++) { GvLook L; if (!gv_hovered(GV.rows[i].item) && gv_sample(&GV.rows[i], &L)) { GV.lnorm = L; memset(&GV.lhov, 0, sizeof GV.lhov); gv_hover_load(); gv_apply(hov && GV.lhov.ok ? &GV.lhov : &GV.lnorm); done = 1; } }
            if (done) { GV.dark_at = g_dark; GV.dark_t = 0; logf_("goal setting: look taken again after the day/night switch"); } else GV.dark_t = now; }
    }
    if (hov && !GV.saw_hover) { GV.saw_hover = 1; logf_("goal setting: mouse first on the row %lu ms after it was added (mouse pointer then: %s)", (unsigned long)(now - GV.built_at), gv_cursor_name()); }
    if (hov && !GV.saw_press && (gt_bool(GV.left, GT_BTN_PRESSED) || gt_bool(GV.right, GT_BTN_PRESSED))) { GV.saw_press = 1; logf_("goal setting: first press on an arrow %lu ms after the row was added (mouse pointer: %s)", (unsigned long)(now - GV.built_at), gv_cursor_name()); }
    if (GV.fast) { int a = gt_clicked(GV.left, &GV.st_l), b = gt_clicked(GV.right, &GV.st_r);
        if (a || b) { static const int nextv[3] = { 1, 2, 0 }, prevv[3] = { 2, 0, 1 };      /* right arrow: On -> No Sound -> Off -> On; left arrow the other way */
            int cur = g_gfx_enabled; if (cur < 0 || cur > 2) cur = 1; g_gfx_enabled = b ? nextv[cur] : prevv[cur];
            gt_settext(GV.val, gv_word()); gv_save(); logf_("goal setting: goal effects switched to %d (1 on, 2 no sound, 0 off)", (int)g_gfx_enabled); } }
    if (hov != GV.hov) { GV.hov = hov; GV.hov_t = now; gv_apply(hov ? &GV.lhov : &GV.lnorm); }
    if (hov && GV.lhov.ok && now - GV.hov_t > 350 && GV.diag[g_dark ? 1 : 0] < 2 && GV.bg && GV.name) {      /* what the row really shows a moment after the hover look was put on */
        GV.diag[g_dark ? 1 : 0]++;
        float bc[4] = {0}, bt[4] = {0}, nc[4] = {0}, shown[4] = {-1, -1, -1, -1}, want_n[4]; uintptr_t sw = 0;
        uintptr_t ic = obj_class(GV.bg), tc = obj_class(GV.name); int io = gt_po(ic, "ColorAndOpacity"), bo = gt_po(ic, "Brush"), co = gt_po(tc, "ColorAndOpacity");
        if (io >= 0) rd(GV.bg + io, bc, 16); if (bo >= 0) rd(GV.bg + bo + g_tint_off, bt, 16); if (co >= 0) rd(GV.name + co, nc, 16);
        if (g_sw_off >= 0 && rdp(GV.name + g_sw_off, &sw) && sw > 0x10000) rd(sw + g_sc_off, shown, 16);
        memcpy(want_n, GV.lhov.ncol, 16);
        logf_("goal setting: hover check (%s, look %s): background colour %.2f %.2f %.2f %.2f (wanted %.2f %.2f %.2f %.2f), its picture tint %.2f %.2f %.2f %.2f; name text %.2f %.2f %.2f %.2f (wanted %.2f %.2f %.2f), drawn as %.2f %.2f %.2f %.2f",
              g_dark ? "Moon" : "Sun", GV.lhov.ok == 1 ? "learned now" : "from last time", bc[0], bc[1], bc[2], bc[3], GV.lhov.bgcol[0], GV.lhov.bgcol[1], GV.lhov.bgcol[2], GV.lhov.bgcol[3], bt[0], bt[1], bt[2], bt[3], nc[0], nc[1], nc[2], nc[3], want_n[0], want_n[1], want_n[2], shown[0], shown[1], shown[2], shown[3]);
    }                   /* the game's own hover look, once it has been seen */
    if (GV.lhov.ok != 1 && !hov && (GV.polls++ & 3) == 0) {                                          /* learn it: a game row with the mouse on its arrows, seen twice in a row */
        int at = -1; for (int i = 0; i < GV.nrows && at < 0; i++) if (gv_hovered(GV.rows[i].item)) at = i;
        if (at >= 0 && at == GV.cand) { GvLook L; if (gv_sample(&GV.rows[at], &L) && (memcmp(L.ncol, GV.lnorm.ncol, 16) || memcmp(L.bgcol, GV.lnorm.bgcol, 16) || memcmp(L.vcol, GV.lnorm.vcol, 16) || memcmp(L.lb, GV.lnorm.lb, 16) || memcmp(L.rb, GV.lnorm.rb, 16) || (L.bsz && memcmp(L.bgbrush, GV.lnorm.bgbrush, L.bsz)))) {
                GV.lhov = L; gv_hover_save(&L); logf_("goal setting: hover look learned from the game (name text %.2f %.2f %.2f, row background %.2f %.2f %.2f %.2f, arrow button background %.2f %.2f %.2f %.2f)", ((float *)L.ncol)[0], ((float *)L.ncol)[1], ((float *)L.ncol)[2], L.bgcol[0], L.bgcol[1], L.bgcol[2], L.bgcol[3], L.lb[0], L.lb[1], L.lb[2], L.lb[3]); } }
        GV.cand = at; }
}
static void goalvid_start(void) {
    wchar_t la[MAX_PATH]; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    swprintf(GV.file, MAX_PATH, L"%ls\\UE4PSO\\goalfx-on.txt", la);
    swprintf(GV.mark, MAX_PATH, L"%ls\\UE4PSO\\goalvid-building.txt", la);
    swprintf(GV.reffile, MAX_PATH, L"%ls\\UE4PSO\\goalvid-ref.txt", la);
    swprintf(GV.hovfile, MAX_PATH, L"%ls\\UE4PSO\\goalvid-hover2.txt", la);
    if (GetFileAttributesW(GV.mark) != INVALID_FILE_ATTRIBUTES) { GV.off = 1; DeleteFileW(GV.mark); logf_("goal setting: it crashed the game last time, so the Video setting is switched off for this run"); }
    FILE *f = _wfopen(GV.file, L"r"); int v = 1; if (f) { if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 2) g_gfx_enabled = v; fclose(f); }
    gv_ref_file(0);
}

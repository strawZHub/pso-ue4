/* Sun / Moon menu theme (every player's game).
 * Moon: white panels and buttons turn black, blue text turns white. Sun: the original colours.
 * A small sun/moon switch sits in the top-right corner of the game window on the main menu.
 * Colours are changed with the widgets' own setters (SetBrushColor, SetBackgroundColor,
 * SetColorAndOpacity) on the game's thread, so the game redraws them normally. The choice is saved
 * in %LOCALAPPDATA%\UE4PSO\theme.txt. */
/* ===== colour engine: every UMG widget, by property, both in templates and in live widgets ===== */
static volatile int g_dark, g_in_menu;
static uintptr_t g_cls_world, g_cls_gi, g_cls_widget; static int32_t o_gi_userid = -1;
static volatile int g_logged_in; static int g_last_menu_log = -1;
static wchar_t g_theme_file[MAX_PATH];
static HWND g_tg_wnd;
static CRITICAL_SECTION g_thcs, g_pcs; static volatile int g_pump_dark = -1, g_pump_dark_seen;
static volatile int g_in_menu_world, g_world_gen, g_shutdown; static volatile DWORD g_world_tick;
enum { CK_BG = 1, CK_TEXT = 2, CK_ACCENT = 3, CK_MUL = 4, CK_HOVER = 5, CK_SCORE = 6, CK_HILITE = 7, CK_SCORE_K = 8, CK_SCORE_D = 9 };
static int is_ourwhite(const float *c);
#define BK0 0.019f
#define BK2 0.022f
static void lin_to_s(const float *l, float *s) { for (int i = 0; i < 3; i++) { float v = l[i]; s[i] = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f; } }
static int col_new(int kind, const float *c, float *out) {        /* 1 = changed */
    memcpy(out, c, 16);
    if (c[3] < 0.02f) return 0;
    float s[3]; lin_to_s(c, s);                                     /* decide on the visible (sRGB) colour */
    float mx = fmaxf(s[0], fmaxf(s[1], s[2])), mn = fminf(s[0], fminf(s[1], s[2]));
    int blue = s[2] > 0.18f && s[2] > s[0] + 0.12f && s[2] > s[1] + 0.06f;
    if (kind == CK_ACCENT) {
        if (blue) { out[0] = out[1] = out[2] = 0.92f; return 1; }
        if (mn > 0.62f && mx - mn < 0.16f) { out[0] = out[1] = BK0; out[2] = BK2; return 1; }
        return 0;
    }
    if (kind == CK_HOVER) {                                          /* hovered / pressed: inverted (white highlight) */
        if (blue) { out[0] = out[1] = out[2] = 0.92f; return 1; }
        if (mn > 0.62f && mx - mn < 0.16f) { out[0] = out[1] = BK0; out[2] = BK2; return 1; }
        return 0;
    }
    if (kind == CK_HILITE) {                                         /* hidden highlight: navy -> white, nothing else */
        if (blue) { out[0] = out[1] = out[2] = 0.92f; return 1; }
        return 0;
    }
    if (kind == CK_SCORE) {                                          /* scoreboard team boxes -> solid white */
        if (is_ourwhite(c) && c[3] > 0.99f) return 0;
        out[0] = out[1] = out[2] = 0.92f; out[3] = 1; return 1;
    }
    if (kind == CK_SCORE_D) {                                        /* time bar: a darker charcoal (#141416), not black */
        if (fabsf(c[0] - 0.0070f) < 0.0015f && fabsf(c[2] - 0.0080f) < 0.0015f && c[3] > 0.99f) return 0;
        out[0] = out[1] = 0.0070f; out[2] = 0.0080f; out[3] = 1; return 1;
    }
    if (kind == CK_SCORE_K) {                                        /* scoreboard score box / timer bar -> solid charcoal */
        if (fabsf(c[0] - BK0) < 0.002f && fabsf(c[2] - BK2) < 0.002f && c[3] > 0.99f) return 0;
        out[0] = out[1] = BK0; out[2] = BK2; out[3] = 1; return 1;
    }
    if (kind == CK_MUL) {                                           /* colour filters: white stays (it changes nothing) */
        if (blue && s[2] < 0.75f) {                                 /* the UI's navy only - bright team blue stays */ out[0] = out[1] = BK0; out[2] = BK2; return 1; }
        return 0;
    }
    if (kind == CK_TEXT) {
        if (s[2] > 0.85f && s[0] < 0.30f && s[1] < 0.45f) return 0;      /* team blue stays team blue */
        if (fabsf(c[0] - BK0) < 0.004f && fabsf(c[1] - BK0) < 0.004f && fabsf(c[2] - BK2) < 0.004f) return 0;   /* our hover text */
        if (blue || mx < 0.25f || (mx < 0.80f && mx - mn < 0.10f)) { out[0] = out[1] = out[2] = 0.92f; if (c[3] >= 0.3f) out[3] = 1; return 1; }   /* blue, dark and grey text -> solid white */
        return 0;
    }
    if (mn > 0.62f && mx - mn < 0.16f) { out[0] = out[1] = BK0; out[2] = BK2; return 1; }  /* white -> black */
    if (blue) { out[0] = out[1] = 0.024f; out[2] = 0.027f; return 1; }                                 /* blue -> dark grey */
    return 0;
}
/* property reflection (UE 4.27 FField / FProperty layout) */
static int fld_type(uintptr_t p, char *out, int cap) { uintptr_t fc = 0; uint32_t n[2]; return rdp(p + 8, &fc) && rd(fc, n, 8) && name_str(n[0], n[1], out, cap); }
static int fld_name(uintptr_t p, char *out, int cap) { uint32_t n[2]; return rd(p + 0x28, n, 8) && name_str(n[0], n[1], out, cap); }
static int ci_has(const char *h, const char *n) { for (; *h; h++) if (!_strnicmp(h, n, strlen(n))) return 1; return 0; }
typedef struct { uintptr_t addr, own; int32_t oidx, oser; float orig[4], dark[4]; uint8_t is_fcolor; } Patch;
static uintptr_t g_tplown; static int32_t g_tplown_idx, g_tplown_ser;   /* the blueprint copy being read now */
static uintptr_t obj_item_rd(int32_t idx);
static Patch *g_tpl; static int g_ntpl, g_captpl;                  /* template (archetype) colours */
static void patch_add(uintptr_t a, const float *o, const float *d, int fc) {
    EnterCriticalSection(&g_pcs);
    if (g_ntpl == g_captpl) { g_captpl = g_captpl ? g_captpl * 2 : 4096; g_tpl = realloc(g_tpl, sizeof(Patch) * g_captpl); }
    Patch *p = &g_tpl[g_ntpl++]; p->addr = a; p->own = g_tplown; p->oidx = g_tplown_idx; p->oser = g_tplown_ser; memcpy(p->orig, o, 16); memcpy(p->dark, d, 16); p->is_fcolor = (uint8_t)fc;
    LeaveCriticalSection(&g_pcs);
}
static float s2l(float v) { return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
/* walk a struct's colour fields; mode 0 = collect into a local buffer copy (buf, base addr = 0), mode 1 = record template patches */
typedef void (*col_cb)(void *ctx, uintptr_t addr, int kind, int is_fcolor);
static int32_t g_res_off = -1, g_tint_off = 0x18, g_isz_off = 8;
static int g_piclog;
static intptr_t g_tint_delta;                                       /* copies: read the original tint at brush + delta */
static int ui_tex_name(const char *rn) {                           /* textures that are plain UI shapes */
    return ci_has(rn, "White") || ci_has(rn, "Box") || ci_has(rn, "Round") || ci_has(rn, "Rect") || ci_has(rn, "Square") || ci_has(rn, "Panel")
        || ci_has(rn, "Bg") || ci_has(rn, "Background") || ci_has(rn, "Fill") || ci_has(rn, "Solid") || ci_has(rn, "Blank") || ci_has(rn, "Pixel")
        || ci_has(rn, "Arrow") || !strncmp(rn, "X1-", 3) || !strncmp(rn, "X2-", 3) || ci_has(rn, "Border") || ci_has(rn, "Line") || ci_has(rn, "Circle");
}
static int tint_white(uintptr_t brush) {
    float t[4]; uint8_t rule = 0;
    if (!rd(brush + g_tint_off, t, 16) || !rd(brush + g_tint_off + 16, &rule, 1)) return 1;
    if (rule) return 1;
    float sv[3]; lin_to_s(t, sv); float mx = fmaxf(sv[0], fmaxf(sv[1], sv[2])), mn = fminf(sv[0], fminf(sv[1], sv[2]));
    return mn > 0.62f && mx - mn < 0.16f;
}
typedef struct { char n[48]; } TexSeen; static TexSeen g_texlog[300]; static int g_ntexlog;
static int brush_is_picture_at(uintptr_t brush) {                 /* a brush showing a real picture (not a UI shape or a tinted icon) */
    uintptr_t res = 0; float sz[2] = {0, 0}; char rn[128] = "", cn[64] = "", on[96] = "";
    if (g_res_off < 0 || !rd(brush + g_res_off, &res, 8) || !res) return 0;
    rd(brush + g_isz_off, sz, 8);
    obj_name(res, rn, sizeof rn); obj_name(obj_class(res), cn, sizeof cn);
    uintptr_t ou = 0; if (rdp(res + 0x20, &ou) && ou) obj_name(ou, on, sizeof on);
    int pic;
    if (ci_has(cn, "Dynamic") || ci_has(cn, "RenderTarget") || ci_has(cn, "Material") || ci_has(on, "Transient")) pic = 1;
    else if (ui_tex_name(rn)) pic = 0;
    else pic = tint_white(brush + g_tint_delta);                   /* tinted = a recolourable icon; untinted = a picture */
    int i; for (i = 0; i < g_ntexlog; i++) if (!strcmp(g_texlog[i].n, rn)) break;
    if (i == g_ntexlog && g_ntexlog < 300) { snprintf(g_texlog[g_ntexlog++].n, 48, "%s", rn); logf_("theme: texture '%s' (%s) %.0fx%.0f -> %s", rn, cn, sz[0], sz[1], pic ? "picture, kept" : "recoloured"); }
    return pic;
}
static int brush_is_icon_at(uintptr_t brush) {                    /* textured, not a plain box: recolour like text */
    uintptr_t res = 0; char rn[128] = "";
    if (g_res_off < 0 || !rd(brush + g_res_off, &res, 8) || !res) return 0;
    obj_name(res, rn, sizeof rn);
    return !(ci_has(rn, "White") || ci_has(rn, "Box") || ci_has(rn, "Round") || ci_has(rn, "Rect") || ci_has(rn, "Square") || ci_has(rn, "Panel")
             || ci_has(rn, "Bg") || ci_has(rn, "Background") || ci_has(rn, "Fill") || ci_has(rn, "Solid") || ci_has(rn, "Blank") || ci_has(rn, "Pixel"));
}
/* checkbox textures (navy pictures) are swapped for made ones while dark */
static volatile int g_rs_on; static void rs_record(uintptr_t brush, int full);
static int rs_match(uintptr_t brush) {
    uintptr_t res = 0; char rn[64] = "";
    if (g_res_off < 0 || !rd(brush + g_res_off, &res, 8) || !res || !obj_name(res, rn, sizeof rn)) return 0;
    if (!strcmp(rn, "Formation")) return 6;                          /* hand-drawn: pitch icon */
    if (!strcmp(rn, "Formation-H")) return 7;
    if (!strcmp(rn, "Back")) return 3;                               /* hand-drawn dark versions */
    if (!strcmp(rn, "Back-H")) return 4;
    static const char *pairs[] = { "Exit", "Export", "List", "Resume", "Settings", 0 };   /* converted from their own pixels */
    for (int i = 0; pairs[i]; i++) { size_t n = strlen(pairs[i]); if (!strncmp(rn, pairs[i], n) && (!rn[n] || !strcmp(rn + n, "-H"))) return 5; }
    if (strncmp(rn, "X1-", 3) && strncmp(rn, "X2-", 3)) return 0;
    return ci_has(rn, "Full") ? 2 : ci_has(rn, "Empty") ? 1 : 0;
}
/* game data colours (hair, shirt, picker...) are never touched */
static int data_colour_name(const char *pn) {
    static const char *k[] = { "Hair", "Skin", "Eye", "Shirt", "Kit", "Short", "Sock", "Shoe", "Boot", "Glove", "Wheel", "Picker", "Hue", "Selected", "Current",
                               "Preview", "Team", "Tag", "Primary", "Secondary", "Jersey", "Value", "Saturat", "Bright", "Custom", "Picked", "Chosen", "New", "Old", "Default", 0 };
    for (int i = 0; k[i]; i++) if (ci_has(pn, k[i])) return 1;
    return 0;
}
static void walk_struct(uintptr_t base, uintptr_t ustruct, int kind, int depth, col_cb cb, void *ctx, int text_owner) {
    char tn[64], pn[96], sn[96];
    for (uintptr_t st = ustruct; st && depth < 7; ) {
        uintptr_t p = 0; rdp(st + 0x50, &p);
        for (int k = 0; p && k < 600; k++) {
            int32_t off = 0, dim = 1, esz = 0;
            if (fld_type(p, tn, sizeof tn) && !strcmp(tn, "StructProperty") && rd(p + 0x4C, &off, 4) && rd(p + 0x38, &dim, 4) && rd(p + 0x3C, &esz, 4) && dim >= 1 && dim <= 8) {
                uintptr_t sp = 0; fld_name(p, pn, sizeof pn); sn[0] = 0;
                if (rdp(p + 0x78, &sp) && sp) obj_name(sp, sn, sizeof sn);
                int k2 = kind;
                if (ci_has(pn, "TextColor") || ci_has(pn, "TextColour")) k2 = k2 ? CK_TEXT : 0;          /* list rows, incl. selected row text */
                else if (data_colour_name(pn) && !ci_has(pn, "Selected")) k2 = 0;
                else if (!strcmp(pn, "ContentColorAndOpacity") || (!text_owner && !strcmp(pn, "ColorAndOpacity"))) k2 = 0;      /* filters over whole panels / pictures */
                else if (!strcmp(pn, "BackgroundColor") || !strcmp(pn, "BrushColor")) k2 = k2 ? CK_MUL : 0;
                else if (ci_has(pn, "Shadow") || ci_has(pn, "Highlight") || ci_has(pn, "Selection") || ci_has(pn, "Selected") && ci_has(pn, "Background")) k2 = 0;
                else if (k2 == CK_BG && (!strcmp(pn, "Hovered") || !strcmp(pn, "Pressed"))) k2 = CK_HOVER;
                else if (ci_has(pn, "Foreground") || ci_has(pn, "TextStyle") || ci_has(pn, "Font") || ci_has(pn, "Caret") || (text_owner && !strcmp(pn, "ColorAndOpacity"))) k2 = CK_TEXT;
                for (int i = 0; i < dim && k2; i++) {
                    uintptr_t a = base + off + (uintptr_t)i * esz;
                    if (!strcmp(sn, "LinearColor")) cb(ctx, a, k2, 0);
                    else if (!strcmp(sn, "SlateColor")) cb(ctx, a, k2 | 0x100, 0);
                    else if (!strcmp(sn, "Color")) cb(ctx, a, k2, 1);
                    else if (!strcmp(sn, "SlateBrush") && g_rs_on && rs_match(a)) { rs_record(a, rs_match(a)); continue; }
                    else if (!strcmp(sn, "SlateBrush") && !g_rs_on && rs_match(a)) continue;     /* swapped, colour left as is */
                    else if (!strcmp(sn, "SlateBrush") && brush_is_picture_at(a)) continue;
                    else if (!strcmp(sn, "SlateBrush") && brush_is_icon_at(a)) walk_struct(a, sp, CK_TEXT, depth + 1, cb, ctx, 1);
                    else if (sp && strcmp(sn, "Vector2D") && strcmp(sn, "Margin") && strcmp(sn, "SlateFontInfo")) walk_struct(a, sp, k2, depth + 1, cb, ctx, ci_has(sn, "Text"));
                }
            }
            if (!rdp(p + 0x20, &p)) break;
        }
        uintptr_t sup = 0; rdp(st + 0x40, &sup); st = sup;
    }
}
/* read / write one colour field (FSlateColor only when it holds a specified colour) */
static int col_read(uintptr_t a, int kind, int fc, float *c) {
    if (kind & 0x100) { uint8_t rule = 1; if (!rd(a + 16, &rule, 1) || rule) return 0; }
    if (fc) { uint8_t b[4]; if (!rd(a, b, 4)) return 0; c[0] = s2l(b[2] / 255.f); c[1] = s2l(b[1] / 255.f); c[2] = s2l(b[0] / 255.f); c[3] = b[3] / 255.f; return 1; }
    return rd(a, c, 16);
}
static void col_write(uintptr_t a, int fc, const float *c) {
    if (fc) { float s[3]; lin_to_s(c, s); uint8_t b[4] = { (uint8_t)(s[2] * 255 + .5f), (uint8_t)(s[1] * 255 + .5f), (uint8_t)(s[0] * 255 + .5f), (uint8_t)(c[3] * 255 + .5f) }; wr_(a, b, 4); }
    else wr_(a, c, 16);
}
static uint64_t tpl_key(uintptr_t o, int is_tpl, int si);
static void torig_put(uint64_t k, const float *c);
static int torig_get(uint64_t k, float *c);
static uintptr_t g_tplcur;
static void tpl_cb(void *ctx, uintptr_t a, int kind, int fc) {
    (void)ctx; float c[4], d[4];
    if (col_read(a, kind, fc, c) && col_new(kind & 0xFF, c, d)) {
        patch_add(a, c, d, fc);
        if (g_tplcur && a > g_tplcur && a - g_tplcur < 0x4000) torig_put(tpl_key(g_tplcur, 1, 0x10000 + (int)(a - g_tplcur)), c);   /* remember the copy's real colour */
    }
}
/* recolour a struct copy: ctx = {dark, cur base, orig base}; each colour field is decided from its original */
typedef struct { int dark; uintptr_t cur, orig; } DualCtx;
static int fld_get(uintptr_t a, int fc, float *c) { if (fc) { uint8_t *b = (uint8_t *)a; c[0] = s2l(b[2] / 255.f); c[1] = s2l(b[1] / 255.f); c[2] = s2l(b[0] / 255.f); c[3] = b[3] / 255.f; } else memcpy(c, (void *)a, 16); return 1; }
static void fld_put(uintptr_t a, int fc, const float *c) { if (fc) { float s_[3]; lin_to_s(c, s_); uint8_t *b = (uint8_t *)a; b[0] = (uint8_t)(s_[2] * 255 + .5f); b[1] = (uint8_t)(s_[1] * 255 + .5f); b[2] = (uint8_t)(s_[0] * 255 + .5f); b[3] = (uint8_t)(c[3] * 255 + .5f); } else memcpy((void *)a, c, 16); }
static int col_eq(const float *a, const float *b) { for (int i = 0; i < 4; i++) if (fabsf(a[i] - b[i]) > 0.004f) return 0; return 1; }
static void copy_cb(void *ctx, uintptr_t a, int kind, int fc) {
    DualCtx *d = (DualCtx *)ctx; uintptr_t ao = a - d->cur + d->orig;
    if ((kind & 0x100) && (*(uint8_t *)(a + 16) || *(uint8_t *)(ao + 16))) return;
    float cur[4], org[4], drk[4];
    fld_get(a, fc, cur); fld_get(ao, fc, org);
    if (!col_new(kind & 0xFF, org, drk)) return;                    /* not a colour the theme touches */
    if (d->dark) { if (col_eq(cur, org)) fld_put(a, fc, drk); }      /* only if the game hasn't changed it */
    else if (col_eq(cur, drk)) fld_put(a, fc, org);                  /* undo only what we set */
}
/* ---- live widgets: the widget's own setter is called with a recoloured copy, originals are kept */
typedef struct { const char *cls, *prop, *setter; int kind; } Setter;
static const Setter SETTERS[] = {
    { "TextBlock", "ColorAndOpacity", "SetColorAndOpacity", CK_TEXT },
    { "Image", "Brush", "SetBrush", CK_BG }, { "Image", "ColorAndOpacity", "SetColorAndOpacity", CK_MUL },
    { "Border", "Brush", "SetBrush", CK_BG }, { "Border", "BrushColor", "SetBrushColor", CK_MUL },
    { "Button", "WidgetStyle", "SetStyle", CK_BG }, { "Button", "BackgroundColor", "SetBackgroundColor", CK_MUL },
    { "Slider", "SliderBarColor", "SetSliderBarColor", CK_TEXT }, { "Slider", "SliderHandleColor", "SetSliderHandleColor", CK_TEXT },
    { "ProgressBar", "FillColorAndOpacity", "SetFillColorAndOpacity", CK_ACCENT },
    { "RichTextBlock", "DefaultTextStyleOverride", "SetDefaultTextStyle", CK_TEXT },
};
#define NSET (sizeof SETTERS / sizeof *SETTERS)
typedef struct { uintptr_t cls, prop, fn, ustruct; int32_t off, size; int is_struct; } SetterRT;
static SetterRT g_set[NSET];
typedef struct { uintptr_t obj; int32_t idx, serial; uint8_t si, applied, tag, kind; uint8_t *orig; } LiveW;
#define MAXLIVE 40000
static LiveW g_live[MAXLIVE]; static volatile LONG g_nlive;
static int32_t g_img_col_off = -1;
static int img_has_picture(uintptr_t o, int32_t brush_off) { uintptr_t res = 0; return g_res_off < 0 || (rdp(o + brush_off + g_res_off, &res) && res); }
static int32_t brush_off_for(const char *cls);
static int img_is_picture_now(uintptr_t o) {                     /* 1 = shows a real picture: never recolour */
    int32_t bo = brush_off_for("Image"); uintptr_t res = 0;
    if (bo < 0 || g_res_off < 0) return 1;
    if (!rdp(o + bo + g_res_off, &res) || !res) return 0;            /* plain colour box */
    return brush_is_picture_at(o + bo);
}
static int img_is_icon(uintptr_t o) {                              /* small UI texture that is not a plain box: recolour like text */
    int32_t bo = brush_off_for("Image"); uintptr_t res = 0; char rn[128] = "";
    if (bo < 0 || g_res_off < 0 || !rdp(o + bo + g_res_off, &res) || !res) return 0;
    obj_name(res, rn, sizeof rn);
    return !(ci_has(rn, "White") || ci_has(rn, "Box") || ci_has(rn, "Round") || ci_has(rn, "Rect") || ci_has(rn, "Square") || ci_has(rn, "Panel")
             || ci_has(rn, "Back") || ci_has(rn, "Bg") || ci_has(rn, "Fill") || ci_has(rn, "Solid") || ci_has(rn, "Blank") || ci_has(rn, "Pixel"));
}
static uintptr_t img_res(uintptr_t o) { int32_t bo = brush_off_for("Image"); uintptr_t r = 0; if (bo >= 0 && g_res_off >= 0) rdp(o + bo + g_res_off, &r); return r; }
static int brush_is_photo(uintptr_t o, int32_t brush_off) {         /* avatars, renders: made at run time */
    uintptr_t res = 0; char cn[96] = "", on[96] = ""; uintptr_t out = 0;
    if (g_res_off < 0) return 1;
    if (!rdp(o + brush_off + g_res_off, &res) || !res) return 0;
    obj_name(obj_class(res), cn, sizeof cn); if (rdp(res + 0x20, &out)) obj_name(out, on, sizeof on);
    return ci_has(cn, "Dynamic") || ci_has(cn, "RenderTarget") || ci_has(cn, "Material") || !strcmp(on, "/Engine/Transient");
}
static int32_t brush_off_for(const char *cls) { for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, cls) && !strcmp(SETTERS[i].prop, "Brush")) return g_set[i].off; return -1; }
static int img_is_picture_now(uintptr_t o);
static int img_brush_si(void) { for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "Image") && !strcmp(SETTERS[i].prop, "Brush")) return (int)i; return -1; }
static int img_is_swatch(uintptr_t o) {                           /* a colour bar the game tints (team colour): keep it bright */
    int32_t co = -1; for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "Image") && !strcmp(SETTERS[i].prop, "ColorAndOpacity")) co = g_set[i].off;
    float c[4], sv[3]; if (co < 0 || !rd(o + co, c, 16)) return 0;
    lin_to_s(c, sv); float mx = fmaxf(sv[0], fmaxf(sv[1], sv[2])), mn = fminf(sv[0], fminf(sv[1], sv[2]));
    if (mx - mn < 0.30f) return 0;
    if (sv[2] > sv[0] + 0.12f && sv[2] < 0.75f && sv[0] < 0.15f && sv[1] < 0.15f) return 0;     /* the UI's own navy */
    return 1;
}
static int g_in_wrap;
/* colours the game computes every frame through a binding ("Get..Color.."): the answer is adjusted on its way back */
typedef struct { uintptr_t fn; int kind; int32_t off; int sc; } BFn;
static BFn g_bfn[1024]; static int g_nbfn;
static void theme_binding_fix(void *self, void *fn, void *parms) {
    if (!g_dark || g_in_wrap || g_shutdown || !fn || !parms || GetCurrentThreadId() != g_gt_id) return;
    BFn *b = NULL;
    static int16_t ht[4096]; static int hinit; if (!hinit) { hinit = 1; for (int i = 0; i < 4096; i++) ht[i] = -1; }
    unsigned hh = (unsigned)(((uintptr_t)fn >> 4) * 2654435761u) >> 20;
    for (int k = 0; k < 4096; k++, hh = (hh + 1) & 4095) { if (ht[hh] < 0) break; if (g_bfn[ht[hh]].fn == (uintptr_t)fn) { b = &g_bfn[ht[hh]]; break; } }
    if (!b) {
        BFn nb = { (uintptr_t)fn, 0, -1, 0 }; char nm[96] = "";
        if (obj_name((uintptr_t)fn, nm, sizeof nm) && !strncmp(nm, "Get", 3) && (ci_has(nm, "Color") || ci_has(nm, "Colour"))) {
            uintptr_t rp = find_prop((uintptr_t)fn, "ReturnValue"), st = 0; char sn[64] = "";
            if (rp && prop_off(rp, &nb.off) && rdp(rp + 0x78, &st) && st && obj_name(st, sn, sizeof sn)) {
                if (!strcmp(sn, "SlateColor")) { nb.sc = 1; nb.kind = CK_TEXT; }
                else if (!strcmp(sn, "LinearColor")) { nb.sc = 0; nb.kind = CK_HILITE; }
                if (nb.kind && (ci_has(nm, "Brush") || ci_has(nm, "Background") || ci_has(nm, "Image") || ci_has(nm, "Border"))) nb.kind = CK_HILITE;
                static int lg; if (nb.kind && lg++ < 40) logf_("theme: colour binding '%s' (%s) adjusted in Moon", nm, sn);
            }
        }
        if (g_nbfn < 1024) { g_bfn[g_nbfn] = nb; b = &g_bfn[g_nbfn];
            unsigned h2 = (unsigned)(((uintptr_t)fn >> 4) * 2654435761u) >> 20; while (ht[h2] >= 0) h2 = (h2 + 1) & 4095; ht[h2] = (int16_t)g_nbfn; g_nbfn++; } else b = &nb;
        if (b == &nb) { if (!nb.kind) return; }
    }
    if (!b->kind || b->off < 0) return;
    float *c = (float *)((uint8_t *)parms + b->off), nc[4];
    if (b->sc && ((uint8_t *)parms)[b->off + 16]) return;
    if (col_new(b->kind, c, nc)) memcpy(c, nc, 16);
    (void)self;
}
/* the Red / Blue team names on the scoreboard get a white outline in Moon */
typedef struct { uintptr_t o; int32_t idx; float orig[4]; } OutL; static OutL g_outl[16]; static int g_noutl;
static void team_outline(uintptr_t o, int dark) {
    static uintptr_t setfont; static int32_t foff = -1, ooff = -1, coff = -1, fsz = 0; static int init;
    char nm[32] = ""; if (!obj_name(o, nm, sizeof nm) || (strcmp(nm, "T1Name") && strcmp(nm, "T2Name"))) return;
    if (!init) { init = 1; uintptr_t cls = obj_class(o), fp_ = find_prop(cls, "Font"), fst = 0;
        if (fp_ && prop_off(fp_, &foff) && rd(fp_ + 0x3C, &fsz, 4) && rdp(fp_ + 0x78, &fst) && fst) {
            uintptr_t op = find_prop(fst, "OutlineSettings"), ost = 0;
            if (op && prop_off(op, &ooff) && rdp(op + 0x78, &ost) && ost) prop_off(find_prop(ost, "OutlineColor"), &coff); }
        uintptr_t ch = 0; rdp(cls + 0x48, &ch);
        for (int n = 0; ch && n < 400; n++) { char fn[48]; if (obj_name(ch, fn, sizeof fn) && !strcmp(fn, "SetFont")) { setfont = ch; break; } uintptr_t nx = 0; if (!rdp(ch + 0x28, &nx)) break; ch = nx; } }
    if (!setfont || foff < 0 || ooff < 0 || coff < 0 || fsz <= 0 || fsz > 0x100) return;
    uint8_t P[0x100]; if (!rd(o + foff, P, fsz)) return;
    float *oc = (float *)(P + ooff + coff), want[4];
    int k = -1; for (int i = 0; i < g_noutl; i++) if (g_outl[i].o == o) k = i;
    if (dark) { if (k < 0 && g_noutl < 16) { k = g_noutl++; g_outl[k].o = o; rd(o + 0x0C, &g_outl[k].idx, 4); memcpy(g_outl[k].orig, oc, 16); } want[0] = want[1] = want[2] = 0.92f; want[3] = 1; }
    else { if (k < 0) return; memcpy(want, g_outl[k].orig, 16); }
    if (!memcmp(oc, want, 16)) return;
    memcpy(oc, want, 16);
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); g_in_wrap = 1; upe((void *)o, (void *)setfont, P); g_in_wrap = 0;
}
typedef struct { uintptr_t addr, umg; int32_t idx; float orig[4]; } SPatch;
static SPatch g_sp[256]; static int g_nsp;
static int exe_vt(uintptr_t p) { uintptr_t vt = 0; return p > 0x10000 && !(p & 7) && rdp(p, &vt) && vt > g_exe && vt < g_exe + 0x4A00000; }
static int is_ui_navy(const uint8_t *b) { const float *f = (const float *)b; return f[0] == 0.0f && f[1] == 0.0f && f[2] > 0.17f && f[2] < 0.26f && fabsf(f[3] - 1) < 1e-4f; }
/* breadth-first through the combo box's drawn widgets: the copied text colour sits in its button */
static int slate_bfs(uintptr_t umg, int32_t idx, uintptr_t *start, int nstart) {
    enum { MAXV = 320 }; static uintptr_t q[MAXV]; static uint8_t qd[MAXV]; int head = 0, tail = 0;
    for (int i = 0; i < nstart && tail < MAXV; i++) { q[tail] = start[i]; qd[tail++] = 0; }
    uint8_t *b = malloc(0x2000); if (!b) return 0;
    int found = 0;
    while (head < tail) {
        uintptr_t sw = q[head]; int depth = qd[head++];
        if (!rd(sw, b, 0x2000)) continue;
        for (int off = 8; off + 24 <= 0x2000; off += 4) {
            if (is_ui_navy(b + off) && b[off + 16] == 0 && g_nsp < 256) {
                static const float wt[4] = { 0.92f, 0.92f, 0.92f, 1 };
                int dup = 0; for (int i = 0; i < g_nsp; i++) if (g_sp[i].addr == sw + off) dup = 1;
                if (!dup) { g_sp[g_nsp].addr = sw + off; g_sp[g_nsp].umg = umg; g_sp[g_nsp].idx = idx; memcpy(g_sp[g_nsp].orig, b + off, 16); g_nsp++; wr_(sw + off, wt, 16); found++; }
            }
        }
        if (depth >= 5) continue;
        for (int off = 8; off + 16 <= 0x2000 && tail < MAXV; off += 8) {
            uintptr_t p = *(uintptr_t *)(b + off), ref = *(uintptr_t *)(b + off + 8);
            if (!exe_vt(p) || !exe_vt(ref)) continue;
            int seen = 0; for (int i = 0; i < tail; i++) if (q[i] == p) { seen = 1; break; }
            if (!seen) { q[tail] = p; qd[tail++] = (uint8_t)(depth + 1); }
        }
    }
    free(b);
    static int lg; if (lg < 8 && found) { lg++; logf_("theme: dropdown: %d drawn parts searched, %d copied navy colours made white", tail, found); }
    return found;
}
static int g_theme_bfs;                                              /* off: this search guessed where colours sit inside drawn widgets and could overwrite other data (the v81 dark-mode crashes) */
static int slate_navy_fix(uintptr_t umg) {                           /* combo boxes built before the theme: their copied colours */
    if (!g_theme_bfs) return 0;
    static uint8_t b[0x1800]; if (!rd(umg, b, sizeof b)) return 0;
    int32_t idx = -1; rd(umg + 0x0C, &idx, 4);
    uintptr_t st[32]; int ns = 0;
    for (int off = 0x28; off + 16 <= (int)sizeof b && ns < 32; off += 8) {
        uintptr_t p = *(uintptr_t *)(b + off), ref = *(uintptr_t *)(b + off + 8);
        if (exe_vt(p) && exe_vt(ref)) st[ns++] = p;
    }
    return slate_bfs(umg, idx, st, ns);
}
typedef struct { uintptr_t o; int32_t idx; uint8_t done, tries; } Combo; static Combo g_combo[64]; static volatile LONG g_ncombo;
static void combo_note(uintptr_t o) {
    for (int i = 0; i < g_ncombo; i++) if (g_combo[i].o == o) return;
    int32_t idx = -1; rd(o + 0x0C, &idx, 4);
    if (g_ncombo < 64) { g_combo[g_ncombo].o = o; g_combo[g_ncombo].idx = idx; g_combo[g_ncombo].done = 0; g_combo[g_ncombo].tries = 0; g_ncombo++; }
    else { g_combo[0].o = o; g_combo[0].idx = idx; }
}
static void combos_fix(void) {                                       /* game thread, Moon */
    for (int i = 0; i < g_ncombo; i++) {
        uintptr_t it = obj_item_rd(g_combo[i].idx), chk = 0; int32_t fl = 0;
        if (it && rdp(it, &chk) && chk == g_combo[i].o && rd(it + 8, &fl, 4) && !(fl & 0x30000000)) { if (!g_combo[i].done && (slate_navy_fix(g_combo[i].o) | (++g_combo[i].tries > 4))) g_combo[i].done = 1; }
        else { g_combo[i] = g_combo[--g_ncombo]; i--; }
    }
}
/* whole menus (user widgets) pass a foreground colour down to texts that inherit it (e.g. the lobby search dropdown) */
static int is_navy(const float *c);
typedef struct { uintptr_t o; int32_t idx; float orig[4]; uint8_t done; } UFg;
static UFg g_ufg[256]; static volatile LONG g_nufg; static uintptr_t g_fn_setfg; static int32_t g_off_uwfg = -1;
static void ufg_note(uintptr_t o, uintptr_t uwc) {
    if (g_off_uwfg < 0) { prop_off(find_prop(uwc, "ForegroundColor"), &g_off_uwfg);
        uintptr_t ch = 0; rdp(uwc + 0x48, &ch);
        for (int n = 0; ch && n < 600; n++) { char fn[48]; if (obj_name(ch, fn, sizeof fn) && !strcmp(fn, "SetForegroundColor")) { g_fn_setfg = ch; break; } uintptr_t nx = 0; if (!rdp(ch + 0x28, &nx)) break; ch = nx; } }
    if (g_off_uwfg < 0) return;
    uint8_t c[20]; if (!rd(o + g_off_uwfg, c, 20) || c[16] != 0 || !is_navy((float *)c)) return;
    for (int i = 0; i < g_nufg; i++) if (g_ufg[i].o == o) return;
    if (g_nufg < 256) { UFg *u = &g_ufg[g_nufg]; u->o = o; rd(o + 0x0C, &u->idx, 4); memcpy(u->orig, c, 16); u->done = 0; g_nufg++; }
}
static void ufg_apply(int dark) {                                     /* game thread */
    if (!g_fn_setfg || g_off_uwfg < 0) return;
    for (int i = 0; i < g_nufg; i++) {
        UFg *u = &g_ufg[i]; uintptr_t it = obj_item_rd(u->idx), chk = 0; int32_t fl = 0;
        if (!(it && rdp(it, &chk) && chk == u->o && rd(it + 8, &fl, 4) && !(fl & 0x30000000))) { g_ufg[i] = g_ufg[--g_nufg]; i--; continue; }
        uint8_t P[0x40]; memset(P, 0, sizeof P); if (!rd(u->o + g_off_uwfg, P, 0x28)) continue;
        float want[4] = { 0.92f, 0.92f, 0.92f, u->orig[3] }; if (!dark) memcpy(want, u->orig, 16);
        if (!memcmp(P, want, 16)) continue;
        memcpy(P, want, 16); P[16] = 0;
        ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); g_in_wrap = 1; upe((void *)u->o, (void *)g_fn_setfg, P); g_in_wrap = 0;
        if (!dark) { g_ufg[i] = g_ufg[--g_nufg]; i--; }
    }
    { static int lg; if (dark && g_nufg && lg < 3) { lg++; logf_("theme: %d menus had a navy foreground -> white", (int)g_nufg); } }
}
static void slate_navy_restore(void) {
    for (int i = 0; i < g_nsp; i++) {
        uintptr_t it = obj_item_rd(g_sp[i].idx), chk = 0; int32_t fl = 0; float c[4];
        if (it && rdp(it, &chk) && chk == g_sp[i].umg && rd(it + 8, &fl, 4) && !(fl & 0x30000000) && rd(g_sp[i].addr, c, 16) && is_ourwhite(c)) wr_(g_sp[i].addr, g_sp[i].orig, 16);
    }
    g_nsp = 0;
}
static int tag_shown(uintptr_t o) {                                 /* a coloured name tag is on this text right now */
    for (int i = 0; i < g_ntw; i++) if (g_tw[i].obj == o) return g_tw[i].mode != 0;
    return 0;
}
/* widgets the add-on built itself and colours itself (the Goal Effects row): the theme keeps its hands off them */
static uintptr_t g_th_skip[48]; static volatile LONG g_th_nskip;
static int th_skip(uintptr_t o) { int n = g_th_nskip > 48 ? 48 : g_th_nskip; for (int i = 0; i < n; i++) if (g_th_skip[i] == o) return 1; return 0; }
static void theme_leave_alone(uintptr_t o) {
    if (!o) return;
    if (!th_skip(o)) { g_th_skip[(g_th_nskip) % 48] = o; g_th_nskip++; }
    EnterCriticalSection(&g_thcs);
    for (int i = 0; i < g_nlive; i++) if (g_live[i].obj == o) g_live[i].applied = 3;
    LeaveCriticalSection(&g_thcs);
}
static int live_apply(LiveW *w, int dark) {                        /* game thread; 1 = changed something */
    SetterRT *s = &g_set[w->si]; static uint8_t cur[2048];
    if (!s->fn || s->size <= 0 || s->size > 1900) return 0;
    int kind = w->kind ? w->kind : SETTERS[w->si].kind, isimg = !strcmp(SETTERS[w->si].cls, "Image");
    if (isimg && dark && !strcmp(SETTERS[w->si].prop, "Brush") && img_is_picture_now(w->obj)) return 0;   /* pictures are never darkened */
    if (isimg && dark && !strcmp(SETTERS[w->si].prop, "Brush") && img_is_swatch(w->obj)) return 0;
    if (isimg && kind == CK_BG && img_is_icon(w->obj)) kind = CK_TEXT;

    if (!rd(w->obj + s->off, cur, s->size)) return 0;
    memset(cur + s->size, 0, 64);
    uint8_t before[2048]; memcpy(before, cur, s->size);
    if (s->is_struct) { DualCtx d = { dark, (uintptr_t)cur, (uintptr_t)w->orig }; g_tint_delta = (intptr_t)w->orig - (intptr_t)cur;
        walk_struct((uintptr_t)cur, s->ustruct, kind, 0, copy_cb, &d, kind == CK_TEXT);
        if (isimg && !dark) { int k2 = kind == CK_TEXT ? CK_BG : CK_TEXT; walk_struct((uintptr_t)cur, s->ustruct, k2, 0, copy_cb, &d, k2 == CK_TEXT); }
        g_tint_delta = 0; }
    else { DualCtx d = { dark, (uintptr_t)cur, (uintptr_t)w->orig }; copy_cb(&d, (uintptr_t)cur, kind | (s->size >= 32 ? 0x100 : 0), 0); }
    if (!memcmp(before, cur, s->size)) return 0;                     /* nothing to change */
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE);
    g_in_wrap = 1; upe((void *)w->obj, (void *)s->fn, cur); g_in_wrap = 0;
    return 1;
}
/* ---- checkbox texture swap */
typedef struct { uintptr_t brush, obj, orig; int32_t idx, ser; int full; int w, h; } RSwap;
#define MAXRS 4096
static RSwap g_rs[MAXRS]; static volatile LONG g_nrs; static struct { uintptr_t obj; int32_t idx, ser; } g_rscur;
static uintptr_t g_tex[6]; static int g_tex_fail; static int32_t g_rh_off = -1;
static uintptr_t g_krl_cdo, g_fn_import, g_brush_struct;
static void rs_record(uintptr_t brush, int full) {
    uintptr_t res = 0; if (!rd(brush + g_res_off, &res, 8) || !res) return;
    EnterCriticalSection(&g_thcs);
    for (int i = 0; i < g_nrs; i++) if (g_rs[i].brush == brush && g_rs[i].ser == g_rscur.ser) { LeaveCriticalSection(&g_thcs); return; }
    if (g_nrs < MAXRS) { RSwap *r = &g_rs[g_nrs]; float sz[2] = {0, 0}; rd(brush + g_isz_off, sz, 8);
        r->brush = brush; r->obj = g_rscur.obj; r->idx = g_rscur.idx; r->ser = g_rscur.ser; r->orig = res; r->full = full;
        r->w = sz[0] >= 8 && sz[0] <= 512 ? (int)sz[0] : 150; r->h = sz[1] >= 8 && sz[1] <= 512 ? (int)sz[1] : 150; g_nrs++; }
    LeaveCriticalSection(&g_thcs);
}
static int rs_alive(RSwap *r) { uintptr_t it = obj_item_rd(r->idx), o = 0; int32_t ser = 0, fl = 0; return it && rdp(it, &o) && o == r->obj && rd(it + 8, &fl, 4) && !(fl & 0x30000000) && rd(it + 0x10, &ser, 4) && ser == r->ser; }
static uint32_t crc32_(const uint8_t *p, size_t n, uint32_t c) { c = ~c; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1; } return ~c; }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static int make_png(const uint8_t *rgba, int W, int H, uint8_t *out) {          /* stored (uncompressed) PNG */
    static uint8_t raw[256 * (1 + 256 * 4) + 64]; int rn = 0; uint8_t *o = out;
    if (W <= 0 || H <= 0 || (size_t)H * (1 + (size_t)W * 4) > sizeof raw) return 0;      /* never write past the work buffer */
    for (int y = 0; y < H; y++) { raw[rn++] = 0; memcpy(raw + rn, rgba + y * W * 4, W * 4); rn += W * 4; }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; memcpy(o, sig, 8); o += 8;
    be32(o, 13); memcpy(o + 4, "IHDR", 4); be32(o + 8, W); be32(o + 12, H); o[16] = 8; o[17] = 6; o[18] = o[19] = o[20] = 0;
    be32(o + 21, crc32_(o + 4, 17, 0)); o += 25;
    uint8_t *len = o; memcpy(o + 4, "IDAT", 4); uint8_t *z = o + 8, *d = z;
    *d++ = 0x78; *d++ = 0x01;
    for (int pos = 0; pos < rn; ) {
        int n = rn - pos; if (n > 65535) n = 65535;
        *d++ = pos + n >= rn; *d++ = n & 255; *d++ = n >> 8; *d++ = ~n & 255; *d++ = (~n >> 8) & 255;
        memcpy(d, raw + pos, n); d += n; pos += n;
    }
    uint32_t A = 1, B = 0; for (int i = 0; i < rn; i++) { A = (A + raw[i]) % 65521; B = (B + A) % 65521; }
    be32(d, (B << 16) | A); d += 4;
    int zl = (int)(d - z); be32(len, zl);
    be32(d, crc32_(len + 4, 4 + zl, 0)); d += 4;
    o = d; be32(o, 0); memcpy(o + 4, "IEND", 4); be32(o + 8, crc32_(o + 4, 4, 0)); o += 12;
    return (int)(o - out);
}
static float seg_cov(float d, float r) { return fminf(1, fmaxf(0, r - d + 0.5f)); }
static uintptr_t make_tex(void *ctxobj, int full) {           /* 0 white box, 1 white box + black X, 2/3 back button (+hover), 4/5 pitch icon (+hover) */
    enum { W = 150 }; static uint8_t px[W * W * 4], png[W * W * 4 + W + 4096];
    const float WH = 245, BK = 38;
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
        uint8_t *p = px + (y * W + x) * 4; float c = WH, fx = x + 0.5f, fy = y + 0.5f;
        if (full == 1) {                                             /* black X */
            float in = W * 0.235f, th = W * 0.066f;
            if (fx > in - 3 && fx < W - in + 3 && fy > in - 3 && fy < W - in + 3) {
                float d1 = fabsf(fx - fy) / 1.41421f, d2 = fabsf(fx + fy - W) / 1.41421f, d = fminf(d1, d2);
                float cov = seg_cov(d, th);
                float lim = fminf(fminf(fx - in, fy - in), fminf(W - in - fx, W - in - fy)); if (lim < 0) cov *= fmaxf(0, 1 + lim);
                c = WH * (1 - cov) + BK * cov;
            }
        } else if (full >= 4) {                                      /* pitch seen in perspective: outline, halfway line, two boxes */
            float sq = full == 4 ? BK : WH, ln = full == 4 ? WH : BK, th = W * 0.022f;
            #define PX(t, yy) ((t) * (1 - ((yy) - 0.26f) / 0.48f) + ((t) + ((t) < 0.5f ? -0.07f : 0.07f)) * (((yy) - 0.26f) / 0.48f))
            float segs[][4] = {
                { PX(0.33f, 0.26f), 0.26f, PX(0.67f, 0.26f), 0.26f }, { PX(0.33f, 0.74f), 0.74f, PX(0.67f, 0.74f), 0.74f },
                { PX(0.33f, 0.26f), 0.26f, PX(0.33f, 0.74f), 0.74f }, { PX(0.67f, 0.26f), 0.26f, PX(0.67f, 0.74f), 0.74f },
                { PX(0.33f, 0.50f), 0.50f, PX(0.67f, 0.50f), 0.50f },
                { PX(0.42f, 0.26f), 0.26f, PX(0.42f, 0.35f), 0.35f }, { PX(0.58f, 0.26f), 0.26f, PX(0.58f, 0.35f), 0.35f }, { PX(0.42f, 0.35f), 0.35f, PX(0.58f, 0.35f), 0.35f },
                { PX(0.40f, 0.74f), 0.74f, PX(0.40f, 0.64f), 0.64f }, { PX(0.60f, 0.74f), 0.74f, PX(0.60f, 0.64f), 0.64f }, { PX(0.40f, 0.64f), 0.64f, PX(0.60f, 0.64f), 0.64f } };
            #undef PX
            float cov = 0;
            for (unsigned q = 0; q < sizeof segs / sizeof segs[0]; q++) {
                float ax = segs[q][0] * W, ay = segs[q][1] * W, bx = segs[q][2] * W, by = segs[q][3] * W, dx = bx - ax, dy = by - ay;
                float t = ((fx - ax) * dx + (fy - ay) * dy) / (dx * dx + dy * dy + 1e-6f); t = t < 0 ? 0 : t > 1 ? 1 : t;
                float ex = ax + t * dx - fx, ey = ay + t * dy - fy, d = sqrtf(ex * ex + ey * ey);
                cov = fmaxf(cov, seg_cov(d, th));
            }
            c = sq * (1 - cov) + ln * cov;
        } else if (full >= 2) {                                      /* square, circle, left arrow */
            float sq = full == 2 ? BK : WH, ci = full == 2 ? WH : BK, ar = full == 2 ? BK : WH;
            float cx = W / 2.f, cy = W / 2.f, R_ = W * 0.30f, dc = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
            float inC = seg_cov(dc, R_);
            /* arrow: triangle head pointing left + shaft */
            float hx0 = cx - R_ * 0.55f, hx1 = cx - R_ * 0.02f, hh = R_ * 0.52f;            /* head from tip x0 to base x1, half height hh */
            float inA = 0;
            if (fx >= hx0 - 1 && fx <= hx1 + 1) { float t = (fx - hx0) / (hx1 - hx0); float half = hh * fmaxf(0, fminf(1, t)); inA = seg_cov(fabsf(fy - cy), half) * seg_cov(hx0 - fx, 0) * seg_cov(fx - hx1, 0); }
            if (fx >= hx1 - 1 && fx <= cx + R_ * 0.55f + 1) { float sh = seg_cov(fabsf(fy - cy), R_ * 0.17f) * seg_cov(fx - (cx + R_ * 0.55f), 0) * seg_cov(hx1 - 1 - fx, 0); inA = fmaxf(inA, sh); }
            c = sq * (1 - inC) + (ci * (1 - inA) + ar * inA) * inC;
        }
        uint8_t v = (uint8_t)(c + .5f); p[0] = p[1] = v; p[2] = (uint8_t)(v < 250 && v > 30 ? v + 2 : v); p[3] = 255;
    }
    int n = make_png(px, W, W, png);
    uintptr_t fw = find_prop(g_fn_import, "WorldContextObject"), fb = find_prop(g_fn_import, "Buffer"), fr = find_prop(g_fn_import, "ReturnValue");
    int32_t ow = -1, ob = -1, orr = -1;
    if (!prop_off(fw, &ow) || !prop_off(fb, &ob) || !prop_off(fr, &orr)) { logf_("theme: texture maker params not found"); return 0; }
    uint8_t P[256]; memset(P, 0, sizeof P);
    *(uintptr_t *)(P + ow) = (uintptr_t)ctxobj;
    struct { void *data; int32_t num, max; } arr = { png, n, n }; memcpy(P + ob, &arr, 16);
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE);
    upe((void *)g_krl_cdo, (void *)g_fn_import, P);
    uintptr_t t = *(uintptr_t *)(P + orr);
    if (t) { int32_t idx = -1; rd(t + 0x0C, &idx, 4); uintptr_t it = obj_item_rd(idx); int32_t fl = 0; if (it && rd(it + 8, &fl, 4)) { fl |= 0x40000000; wr_(it + 8, &fl, 4); } }   /* keep it (root set) */
    logf_("theme: made checkbox texture %d -> %p (%d bytes png)", full, (void *)t, n);
    return t;
}
/* ---- game functions used by the in-game switch and the icon converter */
typedef struct { const char *outer, *name; uintptr_t fn; } FnRef;
enum { F_CREATE, F_SPAWN, F_ADDVP, F_SETPOS, F_SETSIZE, F_INVP, F_OPAC, F_VIS, F_SETTEX, F_VPSCALE,
       F_RTNEW, F_RTBEGIN, F_RTEND, F_RTEXPORT, F_RTREL, F_DRAWTEX, F_TINT, F_N };
static FnRef FNS[F_N] = {
    {"WidgetBlueprintLibrary", "Create"}, {"GameplayStatics", "SpawnObject"}, {"UserWidget", "AddToViewport"},
    {"UserWidget", "SetPositionInViewport"}, {"UserWidget", "SetDesiredSizeInViewport"}, {"UserWidget", "IsInViewport"},
    {"Widget", "SetRenderOpacity"}, {"Widget", "SetVisibility"}, {"Image", "SetBrushFromTexture"}, {"WidgetLayoutLibrary", "GetViewportScale"},
    {"KismetRenderingLibrary", "CreateRenderTarget2D"}, {"KismetRenderingLibrary", "BeginDrawCanvasToRenderTarget"},
    {"KismetRenderingLibrary", "EndDrawCanvasToRenderTarget"}, {"KismetRenderingLibrary", "ExportRenderTarget"},
    {"KismetRenderingLibrary", "ReleaseRenderTarget2D"}, {"Canvas", "K2_DrawTexture"}, {"Image", "SetBrushTintColor"} };
static uintptr_t g_wbl_cdo, g_gs_cdo, g_wll_cdo, g_cls_uw, g_cls_img, g_cls_wtree;
static int fp(int f, const char *p) { int32_t o = -1; if (!FNS[f].fn || !prop_off(find_prop(FNS[f].fn, p), &o)) return -1; return o; }
static int fpsz(int f, const char *p) { uintptr_t pr = FNS[f].fn ? find_prop(FNS[f].fn, p) : 0; int32_t z = 0; if (pr) rd(pr + 0x3C, &z, 4); return z; }
static int fset(uint8_t *P, int f, const char *p, const void *v, int n) { int o = fp(f, p); if (o < 0 || o + n > 1024) return 0; memcpy(P + o, v, n); return 1; }
static void fcall(uintptr_t obj, int f, uint8_t *P) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)obj, (void *)FNS[f].fn, P); }
static int fns_ok(int a, int b) { for (int i = a; i <= b; i++) if (!FNS[i].fn) return 0; return 1; }
typedef struct { void *data; int32_t num, max; } UStr;
static UStr ustr(const wchar_t *w) { UStr u = { (void *)w, (int32_t)wcslen(w) + 1, (int32_t)wcslen(w) + 1 }; return u; }

/* read a Radiance .hdr (flat or RLE) into linear floats */
static float *hdr_read(const uint8_t *b, size_t n, int *W, int *H) {
    size_t i = 0; int w = 0, h = 0;
    while (i + 1 < n) { if (b[i] == '\n' && b[i + 1] == '\n') { i += 2; break; } i++; }
    if (sscanf((const char *)b + i, "-Y %d +X %d", &h, &w) != 2 || w <= 0 || h <= 0 || w > 1024 || h > 1024) return NULL;
    while (i < n && b[i] != '\n') i++; i++;
    float *out = calloc((size_t)w * h * 3, sizeof(float)); uint8_t *line = malloc((size_t)w * 4); if (!out || !line) { free(out); free(line); return NULL; }
    for (int y = 0; y < h; y++) {
        if (i + 4 <= n && b[i] == 2 && b[i + 1] == 2 && ((b[i + 2] << 8) | b[i + 3]) == w && w >= 8 && w < 32768) {
            i += 4;
            for (int c = 0; c < 4; c++) for (int x = 0; x < w && i < n; ) {
                int cnt = b[i++];
                if (cnt > 128) { cnt -= 128; uint8_t v = i < n ? b[i++] : 0; while (cnt-- && x < w) line[(x++) * 4 + c] = v; }
                else while (cnt-- && x < w && i < n) line[(x++) * 4 + c] = b[i++];
            }
        } else { for (int x = 0; x < w; x++) for (int c = 0; c < 4; c++) line[x * 4 + c] = i < n ? b[i++] : 0; }
        for (int x = 0; x < w; x++) { uint8_t *e = line + x * 4; float f = e[3] ? ldexpf(1.0f, e[3] - 136) : 0; for (int c = 0; c < 3; c++) out[((size_t)y * w + x) * 3 + c] = e[c] * f; }
    }
    free(line); *W = w; *H = h; return out;
}
int WINAPI GdipCreateBitmapFromFile(const wchar_t *, void **);
int WINAPI GdipBitmapGetPixel(void *, INT, INT, DWORD *);
int WINAPI GdipGetImageWidth(void *, UINT *);
int WINAPI GdipGetImageHeight(void *, UINT *);
int WINAPI GdipDisposeImage(void *);
static float *img_read_file(const wchar_t *path, int *W, int *H) {           /* .hdr or .png -> linear-ish floats (as stored) */
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0; uint8_t *b = malloc(n + 1); if (!b) { CloseHandle(f); return NULL; }
    ReadFile(f, b, n, &got, NULL); CloseHandle(f);
    float *out = NULL;
    if (got > 10 && !memcmp(b, "#?", 2)) out = hdr_read(b, got, W, H);
    else if (got > 8 && b[0] == 0x89 && b[1] == 'P') {
        void *bmp = NULL; UINT w = 0, h = 0;
        if (!GdipCreateBitmapFromFile(path, &bmp) && bmp) {
            GdipGetImageWidth(bmp, &w); GdipGetImageHeight(bmp, &h);
            if (w && h && w <= 1024 && h <= 1024 && (out = calloc((size_t)w * h * 3, sizeof(float)))) {
                for (UINT y = 0; y < h; y++) for (UINT x = 0; x < w; x++) { DWORD c = 0; GdipBitmapGetPixel(bmp, x, y, &c);
                    float *o = out + ((size_t)y * w + x) * 3; o[0] = ((c >> 16) & 255) / 255.f; o[1] = ((c >> 8) & 255) / 255.f; o[2] = (c & 255) / 255.f; }
                *W = w; *H = h;
            }
            GdipDisposeImage(bmp);
        }
    }
    free(b); return out;
}
/* draw a texture on a cleared render target with the game's canvas, save it, read it back */
static float *tex_capture(void *ctx, uintptr_t tex, int w, int h, float clear, int k) {
    if (!fns_ok(F_RTNEW, F_DRAWTEX) || !g_krl_cdo) return NULL;
    uint8_t P[1024];
    memset(P, 0, sizeof P); uintptr_t c = (uintptr_t)ctx; uint8_t fmt = 6 /* RTF_RGBA16f */; float cc[4] = { clear, clear, clear, 1 };
    fset(P, F_RTNEW, "WorldContextObject", &c, 8); fset(P, F_RTNEW, "Width", &w, 4); fset(P, F_RTNEW, "Height", &h, 4);
    fset(P, F_RTNEW, "Format", &fmt, 1); fset(P, F_RTNEW, "ClearColor", cc, 16);
    fcall(g_krl_cdo, F_RTNEW, P);
    uintptr_t rt = 0; int ro = fp(F_RTNEW, "ReturnValue"); if (ro >= 0) memcpy(&rt, P + ro, 8);
    static int dlog; int dl = dlog < 4; if (dl) dlog++;
    if (!rt) { if (dl) logf_("theme: icon converter: render target not made"); return NULL; }
    memset(P, 0, sizeof P); fset(P, F_RTBEGIN, "WorldContextObject", &c, 8); fset(P, F_RTBEGIN, "TextureRenderTarget", &rt, 8);
    fcall(g_krl_cdo, F_RTBEGIN, P);
    uintptr_t canvas = 0; int co = fp(F_RTBEGIN, "Canvas"), xo = fp(F_RTBEGIN, "Context"), xs = fpsz(F_RTBEGIN, "Context");
    uint8_t ctxs[256]; if (co >= 0) memcpy(&canvas, P + co, 8); if (xo >= 0 && xs > 0 && xs <= 256) memcpy(ctxs, P + xo, xs);
    if (canvas && xo >= 0) {
        uint8_t Q[1024]; memset(Q, 0, sizeof Q);
        float pos[2] = {0, 0}, sz[2] = { (float)w, (float)h }, uv1[2] = {1, 1}, col[4] = {1, 1, 1, 1}, piv[2] = {0.5f, 0.5f}; uint8_t bm = 2; /* translucent */
        fset(Q, F_DRAWTEX, "RenderTexture", &tex, 8); fset(Q, F_DRAWTEX, "ScreenPosition", pos, 8); fset(Q, F_DRAWTEX, "ScreenSize", sz, 8);
        fset(Q, F_DRAWTEX, "CoordinatePosition", pos, 8); fset(Q, F_DRAWTEX, "CoordinateSize", uv1, 8); fset(Q, F_DRAWTEX, "RenderColor", col, 16);
        fset(Q, F_DRAWTEX, "BlendMode", &bm, 1); fset(Q, F_DRAWTEX, "PivotPoint", piv, 8);
        fcall(canvas, F_DRAWTEX, Q);
        memset(Q, 0, sizeof Q); fset(Q, F_RTEND, "WorldContextObject", &c, 8); if (xs > 0) fset(Q, F_RTEND, "Context", ctxs, xs);
        fcall(g_krl_cdo, F_RTEND, Q);
    }
    wchar_t dir[MAX_PATH], name[32], full[MAX_PATH + 40]; GetTempPathW(MAX_PATH, dir); wcscat(dir, L"psotheme"); CreateDirectoryW(dir, NULL);
    swprintf(name, 32, L"cap%d", k);
    for (int e = 0; e < 3; e++) { swprintf(full, MAX_PATH + 40, L"%s\\%s%s", dir, name, e == 0 ? L"" : e == 1 ? L".hdr" : L".png"); DeleteFileW(full); }
    UStr fpth = ustr(dir), fnm = ustr(name);
    memset(P, 0, sizeof P); fset(P, F_RTEXPORT, "WorldContextObject", &c, 8); fset(P, F_RTEXPORT, "TextureRenderTarget", &rt, 8);
    fset(P, F_RTEXPORT, "FilePath", &fpth, 16); fset(P, F_RTEXPORT, "FileName", &fnm, 16);
    fcall(g_krl_cdo, F_RTEXPORT, P);
    memset(P, 0, sizeof P); fset(P, F_RTREL, "TextureRenderTarget", &rt, 8); fcall(g_krl_cdo, F_RTREL, P);
    float *px = NULL; int W = 0, H = 0;
    for (int e = 0; e < 3 && !px; e++) { swprintf(full, MAX_PATH + 40, L"%s\\%s%s", dir, name, e == 0 ? L"" : e == 1 ? L".hdr" : L".png"); px = img_read_file(full, &W, &H); DeleteFileW(full); }
    if (px && (W != w || H != h)) { if (dl) logf_("theme: icon converter: size %dx%d, wanted %dx%d", W, H, w, h); free(px); px = NULL; }
    if (!px && dl) { WIN32_FIND_DATAW fd; wchar_t pat[MAX_PATH + 8]; swprintf(pat, MAX_PATH + 8, L"%s\\*", dir); HANDLE fh = FindFirstFileW(pat, &fd); char names[200] = ""; 
        if (fh != INVALID_HANDLE_VALUE) { do { char a[64]; WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, a, 64, NULL, NULL); if (a[0] != '.' && strlen(names) + strlen(a) < 190) { strcat(names, a); strcat(names, " "); } } while (FindNextFileW(fh, &fd)); FindClose(fh); }
        logf_("theme: icon converter: canvas %p, nothing read back; files: %s", (void *)canvas, names); }
    return px;
}
static float lin2s(float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f; }
/* light icon -> dark icon, done by the game's own drawing (nothing read back):
 * the picture is drawn onto a float surface as  out = clear - k * picture  per colour channel, chosen so
 * the UI's light grey (229) lands on our charcoal and its navy (0,0,128) lands on white; white parts go black.
 * The surface itself then becomes the icon. */
static uintptr_t remap_make(void *ctx, uintptr_t tex, int w, int h) {
    if (!fns_ok(F_RTNEW, F_DRAWTEX) || !g_krl_cdo || !ctx) return 0;
    const float gL = 0.7835f, nB = 0.2158f;                               /* light grey and navy blue, linear */
    float kr = (1.0f - BK0) / gL, kb = (1.0f - BK2) / (gL - nB), ab = 1.0f + kb * nB;
    uint8_t P[1024]; uintptr_t c = (uintptr_t)ctx; uint8_t fmt = 6; /* RTF_RGBA16f */
    float cc[4] = { 1.0f, 1.0f, ab, 1.0f };
    memset(P, 0, sizeof P);
    fset(P, F_RTNEW, "WorldContextObject", &c, 8); fset(P, F_RTNEW, "Width", &w, 4); fset(P, F_RTNEW, "Height", &h, 4);
    fset(P, F_RTNEW, "Format", &fmt, 1); fset(P, F_RTNEW, "ClearColor", cc, 16);
    fcall(g_krl_cdo, F_RTNEW, P);
    uintptr_t rt = 0; int ro = fp(F_RTNEW, "ReturnValue"); if (ro >= 0) memcpy(&rt, P + ro, 8);
    if (!rt) { logf_("theme: icon: surface not made"); return 0; }
    memset(P, 0, sizeof P); fset(P, F_RTBEGIN, "WorldContextObject", &c, 8); fset(P, F_RTBEGIN, "TextureRenderTarget", &rt, 8);
    fcall(g_krl_cdo, F_RTBEGIN, P);
    uintptr_t canvas = 0; int co = fp(F_RTBEGIN, "Canvas"), xo = fp(F_RTBEGIN, "Context"), xs = fpsz(F_RTBEGIN, "Context");
    uint8_t ctxs[256]; if (co >= 0) memcpy(&canvas, P + co, 8); if (xo >= 0 && xs > 0 && xs <= 256) memcpy(ctxs, P + xo, xs);
    if (!canvas || xo < 0) { logf_("theme: icon: no canvas"); return 0; }
    uint8_t Q[1024]; memset(Q, 0, sizeof Q);
    float pos[2] = {0, 0}, sz[2] = { (float)w, (float)h }, uv1[2] = {1, 1}, col[4] = { -kr, -kr, -kb, 1 }, piv[2] = {0.5f, 0.5f}; uint8_t bm = 3; /* additive */
    fset(Q, F_DRAWTEX, "RenderTexture", &tex, 8); fset(Q, F_DRAWTEX, "ScreenPosition", pos, 8); fset(Q, F_DRAWTEX, "ScreenSize", sz, 8);
    fset(Q, F_DRAWTEX, "CoordinatePosition", pos, 8); fset(Q, F_DRAWTEX, "CoordinateSize", uv1, 8); fset(Q, F_DRAWTEX, "RenderColor", col, 16);
    fset(Q, F_DRAWTEX, "BlendMode", &bm, 1); fset(Q, F_DRAWTEX, "PivotPoint", piv, 8);
    fcall(canvas, F_DRAWTEX, Q);
    memset(Q, 0, sizeof Q); fset(Q, F_RTEND, "WorldContextObject", &c, 8); if (xs > 0) fset(Q, F_RTEND, "Context", ctxs, xs);
    fcall(g_krl_cdo, F_RTEND, Q);
    /* not pinned: it belongs to this level and is kept alive by the brushes that show it */
    char rn[64] = ""; obj_name(tex, rn, sizeof rn);
    logf_("theme: dark icon made for '%s' %dx%d -> %p", rn, w, h, (void *)rt);
    return rt;
}
typedef struct { uintptr_t orig, made; int fail; int32_t midx; } Remap;
static Remap g_remap[64]; static int g_nremap;
static int remap_alive(Remap *r) { uintptr_t it = obj_item_rd(r->midx), chk = 0; int32_t fl = 0; return r->made && it && rdp(it, &chk) && chk == r->made && rd(it + 8, &fl, 4) && !(fl & 0x30000000); }
static uintptr_t remap_get(void *ctx, uintptr_t orig, int w, int h, int *budget) {
    static int gen = -1; if (gen != g_world_gen) { gen = g_world_gen; g_nremap = 0; }   /* new level: new icons */
    for (int i = 0; i < g_nremap; i++) if (g_remap[i].orig == orig) { if (g_remap[i].fail || remap_alive(&g_remap[i])) return g_remap[i].made; g_remap[i] = g_remap[--g_nremap]; break; }
    if (*budget <= 0 || !ctx || g_nremap >= 64) return 0;
    (*budget)--;
    Remap *r = &g_remap[g_nremap++]; r->orig = orig; r->made = remap_make(ctx, orig, w, h); r->fail = !r->made; r->midx = -1; if (r->made) rd(r->made + 0x0C, &r->midx, 4);
    return r->made;
}
static void rs_pump(void *self) {                                   /* game thread */
    if (!g_nrs || g_rh_off < 0) return;
    int want = g_dark;
    if (want && (!g_tex[0] || !g_tex[1] || !g_tex[2] || !g_tex[3] || !g_tex[4] || !g_tex[5])) {
        if (g_tex_fail > 3 || !g_krl_cdo || !g_fn_import || !self) return;
        for (int k = 0; k < 6; k++) if (!g_tex[k]) { g_tex[k] = make_tex(self, k); if (!g_tex[k]) g_tex_fail++; break; }      /* one picture per look: six at once was a 30 ms step */
        if (!g_tex[0] || !g_tex[1] || !g_tex[2] || !g_tex[3] || !g_tex[4] || !g_tex[5]) return;
    }
    static uint8_t zero[16]; int budget = 2;                          /* at most 2 new icons per frame */
    EnterCriticalSection(&g_thcs);
    for (int i = 0; i < g_nrs; i++) {
        RSwap *r = &g_rs[i]; if (!rs_alive(r)) continue;
        uintptr_t cur = 0, made = 0; if (!rd(r->brush + g_res_off, &cur, 8)) continue;
        if (r->full <= 4) made = g_tex[r->full - 1];
        else if (r->full >= 6) made = g_tex[r->full - 2];
        else { made = want ? remap_get(self, r->orig, r->w, r->h, &budget) : 0;
               if (!made) { for (int q = 0; q < g_nremap; q++) if (g_remap[q].orig == r->orig) made = g_remap[q].made;
                            if (!made && want) { int fl = 0; for (int q = 0; q < g_nremap; q++) if (g_remap[q].orig == r->orig) fl = g_remap[q].fail;
                                                 char rn[16] = ""; obj_name(r->orig, rn, sizeof rn);
                                                 if (fl && !strncmp(rn, "Back", 4)) made = g_tex[r->full - 1]; } }   /* drawn back button as a fallback */
               if (!made) continue; }
        uintptr_t to = want ? made : r->orig;
        if (cur == to || (cur != r->orig && cur != made)) continue;  /* done, or the game changed it */
        wr_(r->brush + g_res_off, &to, 8); wr_(r->brush + g_rh_off, zero, 16);
    }
    LeaveCriticalSection(&g_thcs);
}
static void rs_cleanup(void) { EnterCriticalSection(&g_thcs); int j = 0; for (int i = 0; i < g_nrs; i++) if (rs_alive(&g_rs[i])) g_rs[j++] = g_rs[i]; g_nrs = j; LeaveCriticalSection(&g_thcs); }
static void templates_set(int dark); static void inst_set(int dark);
static WNDPROC g_game_wp; static HWND g_sub_hwnd;
static uintptr_t g_fn_cgeo, g_sbl_cdo, g_fn_l2a, g_fn_lsize; static int32_t o_w_opac = -1;
#define WM_THEME_NOW (WM_APP + 0x3A1)
/* ---- the game's own colour changes (hover, team colours, lists): adjusted the moment the game makes them */
typedef void (*FNat)(void *, void *, void *);
enum { NM_TEXT = 1, NM_BG, NM_TINT, NM_SLIDER, NM_STYLE };
typedef struct { const char *cls, *fname; int mode; uintptr_t fn; FNat orig; int si; } NHook;
static NHook g_nh[] = {
    { "TextBlock", "SetColorAndOpacity", NM_TEXT }, { "Image", "SetColorAndOpacity", NM_BG }, { "Border", "SetBrushColor", NM_BG },
    { "Button", "SetBackgroundColor", NM_BG }, { "Image", "SetBrushTintColor", NM_TINT }, { "Image", "SetBrush", NM_TINT },
    { "Slider", "SetSliderHandleColor", NM_SLIDER }, { "Slider", "SetSliderBarColor", NM_SLIDER },
    { "Button", "SetStyle", NM_STYLE } };
#define NNH ((int)(sizeof g_nh / sizeof g_nh[0]))
static int g_nnh;
typedef struct { uintptr_t o; int32_t idx; uint8_t si, navy, used, score, bright; float game[4], ours[4], tint[4]; } GCol;
#define GCN 16384
static GCol *g_gcol;
static GCol *gcol_get(uintptr_t o, int si, int make) {
    if (!g_gcol) { if (!make) return NULL; g_gcol = calloc(GCN, sizeof *g_gcol); if (!g_gcol) return NULL; }
    unsigned h = (unsigned)(((o >> 4) ^ (unsigned)si) * 2654435761u) & (GCN - 1);
    for (int k = 0; k < 64; k++, h = (h + 1) & (GCN - 1)) {
        if (g_gcol[h].used && g_gcol[h].o == o && g_gcol[h].si == si) return &g_gcol[h];
        if (!g_gcol[h].used) { if (!make) return NULL; memset(&g_gcol[h], 0, sizeof g_gcol[h]); g_gcol[h].used = 1; g_gcol[h].o = o; g_gcol[h].si = (uint8_t)si; rd(o + 0x0C, &g_gcol[h].idx, 4); return &g_gcol[h]; }
    }
    return NULL;
}
static int is_navy(const float *c) { float sv[3]; lin_to_s(c, sv); return sv[2] > sv[0] + 0.12f && sv[2] > sv[1] + 0.06f && sv[2] < 0.75f && sv[0] < 0.15f && sv[1] < 0.15f; }
static int is_ourwhite(const float *c) { return fabsf(c[0] - 0.92f) < 0.003f && fabsf(c[1] - 0.92f) < 0.003f && fabsf(c[2] - 0.92f) < 0.003f; }
static int owner_is_score(uintptr_t o);
static int si_of(const char *cls, const char *prop) { for (unsigned k = 0; k < NSET; k++) if (!strcmp(SETTERS[k].cls, cls) && !strcmp(SETTERS[k].prop, prop)) return (int)k; return -1; }
static int nh_prop(int i, uintptr_t o, uintptr_t *addr, int *slatecol) {   /* where the colour this setter changes lives */
    const char *c = g_nh[i].cls;
    int si = g_nh[i].mode == NM_TINT ? si_of("Image", "Brush") : g_nh[i].si;
    if (si < 0 || g_set[si].off < 0) return 0;
    if (g_nh[i].mode == NM_TINT) { *addr = o + g_set[si].off + g_tint_off; *slatecol = 1; return 1; }
    *addr = o + g_set[si].off; *slatecol = g_set[si].size >= 32; (void)c; return 1;
}
static void nh_apply(int i, uintptr_t o, const float *nc) {
    uintptr_t a; int sc; if (!nh_prop(i, o, &a, &sc)) return;
    uint8_t P[0x80]; memset(P, 0, sizeof P);
    uintptr_t fn = g_nh[i].fn;
    if (g_nh[i].mode == NM_TINT) { fn = FNS[F_TINT].fn; if (!fn || !rd(a, P, 0x28)) return; memcpy(P, nc, 16); P[16] = 0; }
    else { int sz = sc ? 0x28 : 16; if (!rd(a, P, sz)) return; memcpy(P, nc, 16); if (sc) P[16] = 0; }
    g_in_wrap = 1; ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)o, (void *)fn, P); g_in_wrap = 0;
}
static void nh_set(uintptr_t o, int si, const float *nc) {           /* Sun restore through the plain setter */
    SetterRT *S = &g_set[si]; uint8_t P[0x80]; memset(P, 0, sizeof P);
    if (S->size > 0x80 || !rd(o + S->off, P, S->size)) return;
    memcpy(P, nc, 16); if (S->size >= 32) P[16] = 0;
    g_in_wrap = 1; ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)o, (void *)S->fn, P); g_in_wrap = 0;
}

/* buttons whose blueprint swaps the whole style on hover (Settings "Apply Display Mode"): the new style is darkened in place */
typedef struct { uintptr_t a; uint8_t fc; float game[4], ours[4]; } StPatch;
typedef struct { uintptr_t o; int32_t idx; int n; StPatch p[12]; } StBtn;
static StBtn g_sbtn[24]; static int g_nsbtn;
static StBtn *g_scur;
static void style_cb(void *ctx, uintptr_t a, int kind, int fc) {
    (void)ctx; float c[4], d[4];
    if (!col_read(a, kind, fc, c) || !col_new(kind & 0xFF, c, d)) return;
    col_write(a, fc, d);
    if (g_scur && g_scur->n < 12) { StPatch *q = &g_scur->p[g_scur->n++]; q->a = a; q->fc = (uint8_t)fc; memcpy(q->game, c, 16); memcpy(q->ours, d, 16); }
}
static int sbtn_alive(StBtn *b) { uintptr_t it = obj_item_rd(b->idx), chk = 0; int32_t fl = 0; return b->o && it && rdp(it, &chk) && chk == b->o && rd(it + 8, &fl, 4) && !(fl & 0x30000000); }
static void style_dark(uintptr_t o, int si) {                        /* game thread, right after the game set a button style */
    if (si < 0 || g_set[si].off < 0 || !g_set[si].is_struct) return;
    StBtn *b = NULL;
    for (int i = 0; i < g_nsbtn; i++) if (g_sbtn[i].o == o) { b = &g_sbtn[i]; break; }
    if (!b) { for (int i = 0; i < g_nsbtn; i++) if (!sbtn_alive(&g_sbtn[i])) { b = &g_sbtn[i]; break; }
              if (!b && g_nsbtn < 24) b = &g_sbtn[g_nsbtn++]; }
    if (b) { b->o = o; b->n = 0; rd(o + 0x0C, &b->idx, 4); }
    g_scur = b; walk_struct(o + g_set[si].off, g_set[si].ustruct, CK_BG, 0, style_cb, NULL, 0); g_scur = NULL;
}
static void style_restore_all(void) {                                 /* Sun: the game's own style colours back */
    for (int i = 0; i < g_nsbtn; i++) { StBtn *b = &g_sbtn[i]; if (!sbtn_alive(b)) { b->o = 0; continue; }
        for (int k = 0; k < b->n; k++) { float cur[4]; if (col_read(b->p[k].a, 0, b->p[k].fc, cur) && fabsf(cur[0] - b->p[k].ours[0]) < 0.01f && fabsf(cur[2] - b->p[k].ours[2]) < 0.01f) col_write(b->p[k].a, b->p[k].fc, b->p[k].game); }
        b->n = 0; }
}
static void style_redark_all(void) {                                  /* Moon again: darken the styles the game set */
    int si = si_of("Button", "WidgetStyle");
    for (int i = 0; i < g_nsbtn; i++) { StBtn *b = &g_sbtn[i]; if (b->o && sbtn_alive(b)) style_dark(b->o, si); }
}
/* A highlight box (a settings row's background under the mouse) is a plain box the game colours navy. In Moon that navy
 * becomes white - but the box's own picture tint was darkened with every other background, and white x dark = dark.
 * While the box is the highlight its tint is made bright; when the game takes the highlight away the tint goes back. */
static void img_hover_bright(uintptr_t o, GCol *g, int on) {
    int si = img_brush_si(); if (!g || si < 0) return;
    SetterRT *S = &g_set[si]; if (!S->fn || S->off < 0 || S->size <= 0 || S->size > 1900 || g_tint_off + 16 > S->size) return;
    if (on ? g->bright : !g->bright) return;
    static uint8_t b[2048]; if (!rd(o + S->off, b, S->size)) return; memset(b + S->size, 0, 64);
    float *t = (float *)(b + g_tint_off);
    if (on) { int dim = fmaxf(t[0], fmaxf(t[1], t[2])) < 0.5f;
        static int lg; if (lg++ < 6) { char nm[48] = "", rn[48] = "-"; obj_name(o, nm, sizeof nm); uintptr_t res = img_res(o); if (res) obj_name(res, rn, sizeof rn);
            logf_("theme: highlight box '%s' (picture %s): its own tint is %.2f %.2f %.2f %.2f - %s", nm, rn, t[0], t[1], t[2], t[3], dim ? "made bright while it is the highlight" : "bright already, left as it is"); }
        if (!dim) return;
        memcpy(g->tint, t, 16); t[0] = t[1] = t[2] = 1.0f; g->bright = 1; }
    else { g->bright = 0; if (t[0] != 1.0f || t[1] != 1.0f || t[2] != 1.0f) return; memcpy(t, g->tint, 16); }
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); int w = g_in_wrap; g_in_wrap = 1; upe((void *)o, (void *)S->fn, b); g_in_wrap = w;
}
static void nh_before(int i, void *ctx, float *pre) {
    uintptr_t a; int sc; pre[0] = pre[1] = pre[2] = pre[3] = -1;
    if (g_dark && !g_in_wrap && !g_shutdown && ctx && nh_prop(i, (uintptr_t)ctx, &a, &sc)) memcpy(pre, (void *)a, 16);
}
static void nh_after(int i, void *ctx, const float *pre) {          /* game thread, right after the game set a colour */
    if (!g_dark || g_in_wrap || g_shutdown || !ctx) return;
    if (g_nh[i].mode == NM_STYLE) { char bn[64] = ""; if (obj_name((uintptr_t)ctx, bn, sizeof bn) && ci_has(bn, "Apply")) style_dark((uintptr_t)ctx, g_nh[i].si); return; }   /* only the Apply buttons: other buttons already look right */
    uintptr_t o = (uintptr_t)ctx, a; int sc;
    if (!nh_prop(i, o, &a, &sc)) return;
    float col[4]; memcpy(col, (void *)a, 16);
    if (sc && *(uint8_t *)(a + 16)) return;
    float nc[4]; int ch = 0, navy = 0;
    GCol *g = gcol_get(o, 100 + i, 1);
    int mode = g_nh[i].mode;
    int iconlike = mode == NM_SLIDER || (mode == NM_BG && !strcmp(g_nh[i].cls, "Image") && img_res(o));
    if (mode == NM_TEXT && tag_shown(o)) return;
    if (mode == NM_TEXT || iconlike) {
        float sv[3]; lin_to_s(col, sv);
        int wasnavy = (g && g->navy) || is_ourwhite(pre) || is_navy(pre);
        float mn3 = fminf(sv[0], fminf(sv[1], sv[2])), mx3 = fmaxf(sv[0], fmaxf(sv[1], sv[2]));
        if (mn3 > 0.6f && mx3 - mn3 < 0.12f && !is_ourwhite(col) && wasnavy) {   /* hover: the game turned its navy text/icon white -> dark */
            nc[0] = nc[1] = BK0; nc[2] = BK2; nc[3] = col[3]; ch = 1; navy = 1;
        } else { ch = col_new(CK_TEXT, col, nc); navy = (ch && is_navy(col)) || (!ch && wasnavy && !(sv[0] > 0.98f)); }
    } else if (mode == NM_BG && g && (g->score ? g->score : (g->score = (uint8_t)(owner_is_score(o) + 1))) > 1) {
        ch = col_new(g->score == 2 ? CK_SCORE : g->score == 3 ? CK_SCORE_K : CK_SCORE_D, col, nc);  /* scoreboard boxes */
    } else if (is_navy(col)) {                                         /* highlight / hover / icon: inverted -> white */
        nc[0] = nc[1] = nc[2] = 0.92f; nc[3] = col[3]; ch = 1;
    }
    if (g) { g->navy = (uint8_t)navy; memcpy(g->game, col, 16); memcpy(g->ours, ch ? nc : col, 16); }
    if (ch) nh_apply(i, o, nc);
    if (g && !strcmp(g_nh[i].cls, "Image") && !strcmp(g_nh[i].fname, "SetColorAndOpacity")) img_hover_bright(o, g, ch && is_navy(col) && is_ourwhite(nc) && col[3] > 0.5f);
    { static int hl = 120; if (hl < 120) { char oc[64] = "", nm[48] = ""; uintptr_t ow = o, nx = 0;
        for (int d = 0; d < 6 && rdp(ow + 0x20, &nx) && nx; d++) { ow = nx; char cn2[64]; if (obj_name(obj_class(ow), cn2, sizeof cn2) && strcmp(cn2, "WidgetTree")) { snprintf(oc, sizeof oc, "%s", cn2); break; } }
        if (!strcmp(oc, "SettingsListItem_C") || !strcmp(oc, "Settings_C")) { hl++; obj_name(o, nm, sizeof nm);
            logf_("theme: game set %s.%s '%s' %.2f,%.2f,%.2f,%.2f -> %s", g_nh[i].cls, g_nh[i].fname, nm, col[0], col[1], col[2], col[3], ch ? "changed" : "kept"); } } }
}
static LONGLONG g_tc_hook; static int g_tc_calls; static LONGLONG tc_now(void);
#define NWRAP(i) static void nwrap##i(void *c, void *st, void *r) { if (!g_dark) { g_nh[i].orig(c, st, r); return; } float pre[4]; nh_before(i, c, pre); g_nh[i].orig(c, st, r); LONGLONG t0 = tc_now(); nh_after(i, c, pre); g_tc_hook += tc_now() - t0; g_tc_calls++; }
NWRAP(0) NWRAP(1) NWRAP(2) NWRAP(3) NWRAP(4) NWRAP(5) NWRAP(6) NWRAP(7) NWRAP(8)
static FNat NWRAPS[9] = { nwrap0, nwrap1, nwrap2, nwrap3, nwrap4, nwrap5, nwrap6, nwrap7, nwrap8 };
static void nh_install(void) {
    uintptr_t fns[NNH]; int have = 0;
    for (int i = 0; i < NNH; i++) {
        fns[i] = 0; g_nh[i].si = -1;
        for (unsigned k = 0; k < NSET; k++) if (!strcmp(SETTERS[k].cls, g_nh[i].cls) && !strcmp(SETTERS[k].setter, g_nh[i].fname)) { g_nh[i].si = (int)k; fns[i] = g_set[k].fn; }
        if (!fns[i] && !strcmp(g_nh[i].fname, "SetBrushTintColor")) fns[i] = FNS[F_TINT].fn;
        if (!fns[i] && g_set[si_of(g_nh[i].cls, "Brush") >= 0 ? si_of(g_nh[i].cls, "Brush") : 0].cls) {   /* SetBrush etc: find on the class */
            uintptr_t cls = g_set[si_of(g_nh[i].cls, "Brush") >= 0 ? si_of(g_nh[i].cls, "Brush") : 0].cls; uintptr_t ch = 0; rdp(cls + 0x48, &ch);   /* UStruct::Children (functions) */
            for (int n = 0; ch && n < 400; n++) { char nm[64]; if (obj_name(ch, nm, sizeof nm) && !strcmp(nm, g_nh[i].fname)) { fns[i] = ch; break; } uintptr_t nx = 0; if (!rdp(ch + 0x28, &nx)) break; ch = nx; }
        }
        if (fns[i]) have++;
    }
    int off = -1;                                                       /* UFunction's native code pointer: same slot in all of them */
    for (int o2 = 0x90; o2 <= 0x140 && off < 0; o2 += 8) {
        int okc = 0, bad = 0; uintptr_t v0 = 0;
        for (int i = 0; i < NNH; i++) { if (!fns[i]) continue; uintptr_t v = 0; if (rdp(fns[i] + o2, &v) && v > g_exe + 0x1000 && v < g_exe + 0x4A00000) { okc++; if (!v0) v0 = v; else if (v == v0) bad = 1; } else bad = 1; }
        if (okc >= 3 && !bad) off = o2;
    }
    if (off < 0) { logf_("theme: colour hook: code slot not found"); return; }
    char lst[300] = "";
    for (int i = 0; i < NNH; i++) {
        if (!fns[i]) continue;
        uintptr_t cur = 0; rdp(fns[i] + off, &cur);
        g_nh[i].fn = fns[i]; g_nh[i].orig = (FNat)cur;
        uintptr_t w = (uintptr_t)NWRAPS[i]; wr_(fns[i] + off, &w, 8);
        if (strlen(lst) < 250) { strcat(lst, g_nh[i].cls); strcat(lst, "."); strcat(lst, g_nh[i].fname); strcat(lst, " "); }
    }
    g_nnh = have; logf_("theme: colour hook ready (slot %d): %s", off, lst);
}
static void nh_restore_all(void) {                                    /* Sun: give back the game's own colours */
    if (!g_gcol) return;
    for (int h = 0; h < GCN; h++) {
        GCol *g = &g_gcol[h]; if (!g->used) continue;
        uintptr_t it = obj_item_rd(g->idx), chk = 0; int32_t fl = 0;
        if (!it || !rdp(it, &chk) || chk != g->o || !rd(it + 8, &fl, 4) || (fl & 0x30000000)) { g->used = 0; continue; }
        int i = g->si - 100; if (i < 0 || i >= NNH || !g_nh[i].fn) continue;
        uintptr_t a; int sc; float cur[4]; if (!nh_prop(i, g->o, &a, &sc) || !rd(a, cur, 16)) continue;
        if (!memcmp(cur, g->ours, 16) && memcmp(cur, g->game, 16)) { int d = g_dark; g_dark = 0; nh_apply(i, g->o, g->game); g_dark = d; }
    }
}
static int32_t o_w_vis;
static DWORD g_th_last;
static void theme_pump_(void *self);
static LARGE_INTEGER g_tc_f; static LONGLONG g_tc_pump, g_tc_max; static DWORD g_tc_t;
static LONGLONG tc_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void tc_report(void) {                                       /* how much game-thread time dark mode used, every 10 s while it is noticeable */
    DWORD now = GetTickCount(); if (!g_tc_t) { g_tc_t = now; QueryPerformanceFrequency(&g_tc_f); return; }
    if (now - g_tc_t < 10000) return;
    double f = g_tc_f.QuadPart ? 1000.0 / (double)g_tc_f.QuadPart : 0, secs = (now - g_tc_t) / 1000.0;
    double pm = g_tc_pump * f / secs, hm = g_tc_hook * f / secs, mx = g_tc_max * f;
    static int lg; if (g_dark && lg < 60 && (pm + hm > 1.5 || mx > 4.0)) { lg++; logf_("theme: dark mode used %.2f ms of each second (checks %.2f, the game's own colour changes %.2f in %d calls); longest single step %.1f ms; %ld widgets tracked", pm + hm, pm, hm, (int)(g_tc_calls / secs), mx, g_nlive); }
    g_tc_pump = g_tc_hook = g_tc_max = 0; g_tc_calls = 0; g_tc_t = now;
}
static void theme_pump(void *self) { if (g_shutdown) return; LONGLONG t0 = tc_now(); theme_pump_(self); LONGLONG d = tc_now() - t0; g_tc_pump += d; if (d > g_tc_max) g_tc_max = d; tc_report(); }
static void theme_pump_(void *self) {                                     /* game thread, from the widget-event hook */
    DWORD now = GetTickCount();
    int want = g_dark ? 1 : 0, toggled = want != g_pump_dark;
    static int ever_dark; if (want) ever_dark = 1;
    if (!want && !toggled && !ever_dark) return;                     /* Sun and never switched to Moon: nothing was changed, nothing to do */
    if (!toggled && now - g_th_last < 100) return;
    g_th_last = now;
    if (toggled) {                                                  /* switch clicked: everything in this one frame */
        g_pump_dark = want;
        if (!want && g_pump_dark_seen) { slate_navy_restore(); ufg_apply(0); for (int i = 0; i < g_ncombo; i++) { g_combo[i].done = 0; g_combo[i].tries = 0; } }
        if (!want && g_pump_dark_seen) style_restore_all();
        if (want && g_pump_dark_seen) style_redark_all();
        if (!want && g_pump_dark_seen) { nh_restore_all(); for (int i = 0; i < g_noutl; i++) { uintptr_t oo = g_outl[i].o, it = obj_item_rd(g_outl[i].idx), chk = 0; int32_t fl = 0; if (oo && it && rdp(it, &chk) && chk == oo && rd(it + 8, &fl, 4) && !(fl & 0x30000000)) team_outline(oo, 0); } g_noutl = 0; }
        g_pump_dark_seen = 1;
        EnterCriticalSection(&g_pcs); templates_set(want); inst_set(want); LeaveCriticalSection(&g_pcs);
    }
    rs_pump(self);
    if (!g_nlive) return;
    int done = 0, looked = 0, cap = toggled ? 1 << 30 : 400;        /* changes per frame; widgets with nothing to change are cheap */
    EnterCriticalSection(&g_thcs);
    for (int i = 0; i < g_nlive && done < cap && looked < (toggled ? 1 << 30 : 20000); i++) {
        LiveW *w = &g_live[i];
        if (w->applied == want || w->applied == 3) continue;
        looked++;
        uintptr_t it = obj_item_rd(w->idx), o = 0; int32_t fl = 0;
        if (!(it && rdp(it, &o) && o == w->obj && rd(it + 8, &fl, 4) && !(fl & 0x30000000))) { w->applied = (uint8_t)want; continue; }   /* gone */
        done += live_apply(w, want); w->applied = (uint8_t)want;
    }
    static DWORD rc; static int rpos;                               /* images that received a picture later (avatars, thumbnails) */
    if (want && now - rc >= 500) {
        rc = now; int isi = img_brush_si(), work = 0;
        for (int n = 0; n < 6000 && g_nlive && work < 120; n++, rpos++) {
            if (rpos >= g_nlive) rpos = 0;
            LiveW *w = &g_live[rpos];
            if (w->applied != 1 || w->si != (uint8_t)isi) continue;
            work++;
            uintptr_t it = obj_item_rd(w->idx), o = 0; int32_t ser = 0;
            if (!(it && rdp(it, &o) && o == w->obj)) continue; (void)ser;
            int32_t bo = brush_off_for("Image"); uintptr_t ores = 0;
            if (bo < 0) continue;
            if (img_is_swatch(o)) { live_apply(w, 0); w->applied = 3; continue; }      /* team colour bar: keep it bright */
            memcpy(&ores, w->orig + g_res_off, 8);
            uintptr_t cres = img_res(o);
            if (!cres || cres == ores) continue;
            g_tint_delta = (intptr_t)w->orig - (intptr_t)(o + bo);
            int pic = brush_is_picture_at(o + bo); g_tint_delta = 0;
            if (pic) { live_apply(w, 0); w->applied = 3; }   /* back to the game's own colours, then left alone */
        }
    }
    { static DWORD cf; if (want && now - cf >= 700) { cf = now; ufg_apply(1); if (g_in_menu_world) combos_fix(); } }
    static DWORD dg; static int dglines = 80;                       /* (diagnostic: off) */
    if (want && dglines < 80 && now - dg >= 3000) {
        dg = now;
        static uintptr_t seenh[512]; static int nseen;
        for (int n = 0; n < g_nlive && dglines < 80; n++) {
            LiveW *w = &g_live[n]; SetterRT *S = &g_set[w->si];
            uintptr_t it = obj_item_rd(w->idx), o = 0;
            if (!(it && rdp(it, &o) && o == w->obj)) continue;
            float c[4]; uintptr_t a = o + S->off;
            if (!strcmp(SETTERS[w->si].prop, "Brush")) a += g_tint_off; else if (!strcmp(SETTERS[w->si].prop, "WidgetStyle")) continue;
            if (!rd(a, c, 16) || !is_navy(c)) continue;
            uint8_t v = 0; if (o_w_vis >= 0 && rd(o + o_w_vis, &v, 1) && (v == 1 || v == 2)) continue;
            char nm[64] = "", oc[64] = ""; obj_name(o, nm, sizeof nm);
            uintptr_t ow = o, nx = 0; for (int d = 0; d < 6 && rdp(ow + 0x20, &nx) && nx; d++) { ow = nx; char cn2[64]; if (obj_name(obj_class(ow), cn2, sizeof cn2) && strcmp(cn2, "WidgetTree")) { snprintf(oc, sizeof oc, "%s", cn2); break; } }
            uintptr_t hh = 0; for (const char *q = nm; *q; q++) hh = hh * 131 + (unsigned char)*q; for (const char *q = oc; *q; q++) hh = hh * 131 + (unsigned char)*q; hh = hh * 7 + w->si;
            int dup = 0; for (int k = 0; k < nseen; k++) if (seenh[k] == hh) dup = 1;
            if (dup) continue; if (nseen < 512) seenh[nseen++] = hh;
            logf_("theme: still navy: %s '%s' in %s (%s, entry %d)", SETTERS[w->si].cls, nm, oc, SETTERS[w->si].prop, w->applied);
            dglines++;
        }
    }
    static DWORD dt; static int dpos, tsi = -2;                     /* text the game recolours later (host menu, lists, scoreboard) */
    if (tsi == -2) { tsi = -1; for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "TextBlock")) tsi = (int)i; }
    if (want && tsi >= 0 && now - dt >= 200) {
        dt = now; int work = 0;
        for (int n = 0; n < 6000 && g_nlive && work < 250; n++, dpos++) {
            if (dpos >= g_nlive) dpos = 0;
            LiveW *w = &g_live[dpos];
            if (w->applied == 3) continue;
            work++;
            if (w->kind == CK_SCORE || w->kind == CK_SCORE_K || w->kind == CK_SCORE_D) {        /* scoreboard boxes: always the design colour */
                uintptr_t it3 = obj_item_rd(w->idx), o3 = 0; if (!(it3 && rdp(it3, &o3) && o3 == w->obj)) continue;
                float c3[4], n3[4]; if (!rd(o3 + g_set[w->si].off, c3, 16) || !col_new(w->kind, c3, n3)) continue;
                uint8_t P3[0x40]; memset(P3, 0, sizeof P3); if (g_set[w->si].size > 0x40 || !rd(o3 + g_set[w->si].off, P3, g_set[w->si].size)) continue;
                memcpy(P3, n3, 16); if (g_set[w->si].size >= 32) P3[16] = 0;
                ch_pe_fn upe3 = (ch_pe_fn)(g_exe + RVA_PE); g_in_wrap = 1; upe3((void *)o3, (void *)g_set[w->si].fn, P3); g_in_wrap = 0;
                w->applied = 1; continue;
            }
            if (w->si != (uint8_t)tsi) {                                 /* tinted icons and sliders: navy left on screen -> white */
                int icon = (w->kind == CK_TEXT || w->kind == CK_HILITE) && !strcmp(SETTERS[w->si].cls, "Image") && !strcmp(SETTERS[w->si].prop, "ColorAndOpacity"), sl = !strcmp(SETTERS[w->si].cls, "Slider");
                if (!icon && !sl) continue;
                uintptr_t it2 = obj_item_rd(w->idx), o2 = 0; if (!(it2 && rdp(it2, &o2) && o2 == w->obj)) continue;
                float c2[4]; if (!rd(o2 + g_set[w->si].off, c2, 16) || !is_navy(c2)) continue;
                float n2[4] = { 0.92f, 0.92f, 0.92f, c2[3] }; uint8_t P2[0x40]; memset(P2, 0, sizeof P2);
                if (g_set[w->si].size > 0x40 || !rd(o2 + g_set[w->si].off, P2, g_set[w->si].size)) continue;
                memcpy(w->orig, c2, 16); memcpy(P2, n2, 16); if (g_set[w->si].size >= 32) P2[16] = 0;
                ch_pe_fn upe2 = (ch_pe_fn)(g_exe + RVA_PE); g_in_wrap = 1; upe2((void *)o2, (void *)g_set[w->si].fn, P2); g_in_wrap = 0;
                w->applied = 1; continue;
            }

            uintptr_t it = obj_item_rd(w->idx), o = 0; int32_t ser = 0;
            if (!(it && rdp(it, &o) && o == w->obj)) continue; (void)ser;
            if (!w->tag) { char nm[48] = ""; obj_name(o, nm, sizeof nm); w->tag = (!_stricmp(nm, "UserId") || !_stricmp(nm, "PlayerId")) ? 1 : (!strcmp(nm, "T1Name") || !strcmp(nm, "T2Name")) ? 3 : 2; }
            if (w->tag == 3) team_outline(o, 0);                       /* team names keep their black outline */
            if (w->tag == 1 && tag_shown(o)) continue;                 /* showing a name-tag colour: the tag code owns it */
            float col[4]; uint8_t rule = 0; uintptr_t sw = 0;
            if (g_sw_off >= 0 && g_sc_off >= 0 && rdp(o + g_sw_off, &sw) && sw > 0x10000) { if (!rd(sw + g_sc_off, col, 16) || !rd(sw + g_sc_off + 16, &rule, 1)) continue; }
            else { if (!rd(o + g_set[tsi].off, col, 16) || !rd(o + g_set[tsi].off + 16, &rule, 1)) continue; }
            if (rule) continue;
            float nc[4]; if (!col_new(CK_TEXT, col, nc)) continue;
            uint8_t P[0x40]; memset(P, 0, sizeof P);
            if (g_set[tsi].size > 0x40 || !rd(o + g_set[tsi].off, P, g_set[tsi].size)) continue;
            memcpy(w->orig, col, 16); w->orig[16] = 0;                  /* Sun puts the game's colour back */
            memcpy(P, nc, 16); P[16] = 0;
            ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); g_in_wrap = 1; upe((void *)o, (void *)g_set[tsi].fn, P); g_in_wrap = 0;
            w->applied = 1;
        }
    }
    LeaveCriticalSection(&g_thcs);
}
typedef struct { uintptr_t obj, addr; int32_t idx, serial; float orig[4], dark[4]; uint8_t fc; } IPatch;
static IPatch *g_ip; static int g_nip, g_capip;
static struct { uintptr_t obj; int32_t idx, serial; } g_ipcur;
static void inst_cb(void *ctx, uintptr_t a, int kind, int fc) {
    (void)ctx; float c[4], d[4];
    if (!col_read(a, kind, fc, c)) return;
    if (!col_new(kind & 0xFF, c, d)) {                               /* born from a darkened copy? then its real colour is the copy's */
        float oc[4], od[4]; uintptr_t o = g_ipcur.obj;
        if (!(g_dark && o && a > o && a - o < 0x4000 && torig_get(tpl_key(o, 0, 0x10000 + (int)(a - o)), oc) && col_new(kind & 0xFF, oc, od) && !memcmp(od, c, 16))) return;
        memcpy(d, od, 16); memcpy(c, oc, 16);
    }
    EnterCriticalSection(&g_pcs);
    if (g_nip == g_capip) { g_capip = g_capip ? g_capip * 2 : 8192; g_ip = realloc(g_ip, sizeof(IPatch) * g_capip); }
    IPatch *p = &g_ip[g_nip++]; p->obj = g_ipcur.obj; p->idx = g_ipcur.idx; p->serial = g_ipcur.serial; p->addr = a; memcpy(p->orig, c, 16); memcpy(p->dark, d, 16); p->fc = (uint8_t)fc;
    if (g_dark) col_write(a, fc, d);
    LeaveCriticalSection(&g_pcs);
}
static int ip_alive(IPatch *p) { uintptr_t it = obj_item_rd(p->idx), o = 0; int32_t ser = 0; return it && rdp(it, &o) && o == p->obj && rd(it + 0x10, &ser, 4) && ser == p->serial; }
static void inst_set(int dark) {
    for (int i = 0; i < g_nip; i++) { IPatch *p = &g_ip[i]; float c[4];
        if (!ip_alive(p) || !col_read(p->addr, 0, p->fc, c)) continue;
        if (dark ? col_eq(c, p->orig) : col_eq(c, p->dark)) col_write(p->addr, p->fc, dark ? p->dark : p->orig); }
}
static void inst_cleanup(void) { EnterCriticalSection(&g_pcs); int j = 0; for (int i = 0; i < g_nip; i++) if (ip_alive(&g_ip[i])) g_ip[j++] = g_ip[i]; g_nip = j; LeaveCriticalSection(&g_pcs); }
static int class_kind_for(const char *nm) {
    if (ci_has(nm, "CheckBox") || ci_has(nm, "ScrollBar") || ci_has(nm, "ScrollBox") || ci_has(nm, "Slider") || ci_has(nm, "Progress") || ci_has(nm, "SpinBox") || ci_has(nm, "Throbber")) return CK_ACCENT;
    return CK_BG;
}
static int patch_alive(const Patch *p) {                          /* the blueprint copy this colour sits in still exists, and the address is inside it */
    uintptr_t it = obj_item_rd(p->oidx), o = 0; int32_t ser = 0, fl = 0;
    return p->own && p->addr > p->own && p->addr - p->own < 0x8000 && it && rdp(it, &o) && o == p->own && rd(it + 8, &fl, 4) && !(fl & 0x30000000) && rd(it + 0x10, &ser, 4) && ser == p->oser;
}
static void templates_set(int dark) {                             /* template memory; colours of copies the game has thrown away are forgotten, never written */
    int j = 0, gone = 0;
    for (int i = 0; i < g_ntpl; i++) {
        Patch *p = &g_tpl[i];
        if (!patch_alive(p)) { gone++; continue; }
        float c[4]; if (col_read(p->addr, 0, p->is_fcolor, c) && (p->is_fcolor || col_eq(c, p->orig) || col_eq(c, p->dark))) col_write(p->addr, p->is_fcolor, dark ? p->dark : p->orig);
        if (j != i) g_tpl[j] = *p; j++;
    }
    g_ntpl = j;
    if (gone) { static int lg; if (lg++ < 20) logf_("theme: %d saved colours belonged to menus the game has removed: forgotten", gone); }
}
/* ---- is the main menu's Exit button on screen? (the switch hides while a sub-menu covers it) */
static int32_t o_w_vis = -1, o_w_slot = -1, o_ps_parent = -1, o_sw_active = -1, o_pw_slots = -1, o_tb_text = -1;
static uintptr_t g_cls_switcher, g_cls_panelslot, g_cls_panelw;
static int chain_visible(uintptr_t w, char *root, int cap) {
    root[0] = 0;
    for (int d = 0; w && d < 48; d++) {
        uint8_t v = 0; if (o_w_vis >= 0 && rd(w + o_w_vis, &v, 1) && (v == 1 || v == 2)) return 0;
        uintptr_t slot = 0; rdp(w + o_w_slot, &slot);
        if (slot) {
            uintptr_t par = 0; if (!rdp(slot + o_ps_parent, &par) || !par) return 1;
            if (g_cls_switcher && obj_class(par) == g_cls_switcher && o_sw_active >= 0 && o_pw_slots >= 0) {
                int32_t act = -1; TArr sl; rd(par + o_sw_active, &act, 4);
                if (rd(par + o_pw_slots, &sl, sizeof sl) && act >= 0 && act < sl.num) { uintptr_t s2 = 0; rdp(sl.ptr + 8ULL * act, &s2); if (s2 != slot) return 0; }
            }
            w = par; continue;
        }
        uintptr_t outer = 0; char on[64];
        if (rdp(w + 0x20, &outer) && outer && obj_name(obj_class(outer), on, sizeof on) && !strcmp(on, "WidgetTree")) {
            uintptr_t uw = 0; if (!rdp(outer + 0x20, &uw) || !uw) return 1;
            obj_name(obj_class(uw), root, cap); w = uw; continue;
        }
        return 1;
    }
    return 1;
}
static volatile int g_exit_shown; static char g_exit_root[96];
static volatile uintptr_t g_exit_obj; static volatile int32_t g_exit_idx, g_exit_ser;
static int exit_poll(void) {                                       /* fast check of the remembered Exit */
    uintptr_t o = g_exit_obj; char root[96];
    if (!o) return 0;
    uintptr_t it = obj_item_rd(g_exit_idx), chk = 0; int32_t ser = 0;
    if (!it || !rdp(it, &chk) || chk != o || !rd(it + 0x10, &ser, 4) || ser != g_exit_ser) { g_exit_obj = 0; return 0; }
    return chain_visible(o, root, sizeof root);
}
/* sub-menus that open on top without hiding the main buttons */
static volatile int g_overlay_menu; static char g_vis_names[600], g_vis_last[600];
/* ---- scanning */
#define KCN 8192
static uintptr_t g_kck[KCN]; static int8_t g_kcv[KCN];
static int widget_kind(uintptr_t cls) {                          /* -1 not a widget, else setter class index base or 99 */
    unsigned h = (unsigned)((cls >> 4) * 2654435761u) & (KCN - 1);
    for (int k = 0; k < 32; k++, h = (h + 1) & (KCN - 1)) { if (g_kck[h] == cls) return g_kcv[h]; if (!g_kck[h]) break; }
    int v = -1; char nm[96]; uintptr_t c = cls;
    for (int d = 0; c && d < 16; d++) { if (c == g_cls_widget) { v = 99; break; } uintptr_t s = 0; rdp(c + 0x40, &s); c = s; }
    if (v == 99 && obj_name(cls, nm, sizeof nm)) for (unsigned i = 0; i < NSET; i++) if (!strcmp(nm, SETTERS[i].cls)) { v = (int)i; break; }
    if (!g_kck[h]) { g_kck[h] = cls; g_kcv[h] = (int8_t)v; }
    return v;
}
typedef struct { uintptr_t o; } Seen;
#define LSN (1 << 16)
static struct { uintptr_t o; int32_t ser; uint16_t mask; } *g_lseen;
static int live_seen(uintptr_t o, int32_t ser, int si) {          /* 1 = already have this widget + setter */
    if (!g_lseen) g_lseen = calloc(LSN, sizeof *g_lseen);
    unsigned h = (unsigned)((o >> 4) * 2654435761u) & (LSN - 1);
    for (int k = 0; k < 256; k++, h = (h + 1) & (LSN - 1)) {
        if (g_lseen[h].o == o) { if (g_lseen[h].ser != ser) { if (g_lseen[h].ser == 0 || ser == 0) { if (ser) g_lseen[h].ser = ser; } else { g_lseen[h].ser = ser; g_lseen[h].mask = 0; } } if (g_lseen[h].mask & (1u << si)) return 1; g_lseen[h].mask |= (uint16_t)(1u << si); return 0; }
        if (!g_lseen[h].o) { g_lseen[h].o = o; g_lseen[h].ser = ser; g_lseen[h].mask = (uint16_t)(1u << si); return 0; }
    }
    return 1;
}
#define SEENN (1 << 17)
static uintptr_t *g_seen_tpl;                                    /* templates already recorded */
static int seen_tpl(uintptr_t o) {
    if (!g_seen_tpl) g_seen_tpl = calloc(SEENN, sizeof(uintptr_t));
    unsigned h = (unsigned)((o >> 4) * 2654435761u) & (SEENN - 1);
    for (int k = 0; k < 64; k++, h = (h + 1) & (SEENN - 1)) { if (g_seen_tpl[h] == o) return 1; if (!g_seen_tpl[h]) { g_seen_tpl[h] = o; return 0; } }
    return 1;
}
typedef struct { int worlds_menu, worlds_match, logged, new_tpl, exit_vis, overlay; char vis[600]; } ThScan;
static void rs_noop_cb(void *c, uintptr_t a, int k, int f) { (void)c; (void)a; (void)k; (void)f; }
static int slider_excluded(uintptr_t o) {
    char oc[96] = ""; uintptr_t ow = o, nx = 0;
    for (int d = 0; d < 6 && rdp(ow + 0x20, &nx) && nx; d++) { ow = nx; char cn2[96]; if (obj_name(obj_class(ow), cn2, sizeof cn2) && strcmp(cn2, "WidgetTree")) { snprintf(oc, sizeof oc, "%s", cn2); break; } }
    return !strcmp(oc, "MainMenu_C") || ci_has(oc, "Music") || ci_has(oc, "Song") || ci_has(oc, "Audio") || ci_has(oc, "Volume") || ci_has(oc, "Radio");
}
static void owner_name(uintptr_t o, char *oc, int n);
static int owner_is_score(uintptr_t o) {                           /* scoreboard parts: 1 = make white, 2 = make charcoal */
    char nm[64] = ""; if (!obj_name(o, nm, sizeof nm)) return 0;
    if (!_stricmp(nm, "WhiteBG")) return 1;                                                           /* boxes around the team names -> white */
    if (!_stricmp(nm, "BlueBG")) return 2;                                                              /* score box -> charcoal */
    if (!_stricmp(nm, "BlackBG") || !_stricmp(nm, "MatchStateBackground")) return 3;                    /* time / match state bar -> darker charcoal */
    return 0;
}
/* original colours of blueprint copies, so widgets born from a darkened copy still get their true colour back in Sun */
typedef struct { uint64_t k; float c[4]; } TOrig;
#define TON 8192
static TOrig *g_torig;
static void owner_name(uintptr_t o, char *oc, int n);
static uint64_t tpl_key(uintptr_t o, int is_tpl, int si) {          /* widget name + the menu class it belongs to */
    char nm[64] = "", oc[96] = ""; obj_name(o, nm, sizeof nm);
    uintptr_t tree = 0, own = 0; rdp(o + 0x20, &tree);
    if (is_tpl) { if (tree) rdp(tree + 0x20, &own); if (own) obj_name(own, oc, sizeof oc); }
    else owner_name(o, oc, sizeof oc);
    uint64_t h = 1469598103934665603ull; for (const char *q = nm; *q; q++) h = (h ^ (unsigned char)*q) * 1099511628211ull;
    h = (h ^ '/') * 1099511628211ull; for (const char *q = oc; *q; q++) h = (h ^ (unsigned char)*q) * 1099511628211ull;
    return h ^ (uint64_t)si * 0x9E37ull;
}
static void torig_put(uint64_t k, const float *c) {
    if (!g_torig) g_torig = calloc(TON, sizeof *g_torig); if (!g_torig) return;
    unsigned h = (unsigned)(k * 2654435761u) & (TON - 1);
    for (int i = 0; i < 64; i++, h = (h + 1) & (TON - 1)) if (!g_torig[h].k || g_torig[h].k == k) { g_torig[h].k = k; memcpy(g_torig[h].c, c, 16); return; }
}
static int torig_get(uint64_t k, float *c) {
    if (!g_torig) return 0; unsigned h = (unsigned)(k * 2654435761u) & (TON - 1);
    for (int i = 0; i < 64; i++, h = (h + 1) & (TON - 1)) { if (!g_torig[h].k) return 0; if (g_torig[h].k == k) { memcpy(c, g_torig[h].c, 16); return 1; } }
    return 0;
}
static uintptr_t inst_key(uintptr_t o) {                           /* this widget, not an older one at the same address */
    uintptr_t outer = 0; rdp(o + 0x20, &outer);
    return o ^ (outer << 17) ^ ((uintptr_t)obj_class(o) << 33) ^ 0x9E3779B97F4A7C15ull;
}
static void owner_name(uintptr_t o, char *oc, int n) {
    oc[0] = 0; uintptr_t ow = o, nx = 0;
    for (int d = 0; d < 6 && rdp(ow + 0x20, &nx) && nx; d++) { ow = nx; char cn2[96]; if (obj_name(obj_class(ow), cn2, sizeof cn2) && strcmp(cn2, "WidgetTree")) { snprintf(oc, n, "%s", cn2); return; } }
}
static int g_dumpn;
static void dump_struct(uintptr_t base, uintptr_t ustruct, const char *pre, int depth, char *out, int cap) {
    char tn[64], pn[96], sn[96];
    for (uintptr_t st = ustruct; st && depth < 3; ) {
        uintptr_t p = 0; rdp(st + 0x50, &p);
        for (int k = 0; p && k < 300; k++) {
            int32_t off = 0;
            if (fld_type(p, tn, sizeof tn) && !strcmp(tn, "StructProperty") && rd(p + 0x4C, &off, 4)) {
                uintptr_t sp = 0; fld_name(p, pn, sizeof pn); sn[0] = 0; if (rdp(p + 0x78, &sp) && sp) obj_name(sp, sn, sizeof sn);
                uintptr_t a = base + off; char buf[200] = "";
                if (!strcmp(sn, "LinearColor") || !strcmp(sn, "SlateColor")) { float c[4]; uint8_t r = 0; rd(a, c, 16); if (!strcmp(sn, "SlateColor")) rd(a + 16, &r, 1);
                    snprintf(buf, sizeof buf, "%s%s=%.2f,%.2f,%.2f,%.2f%s ", pre, pn, c[0], c[1], c[2], c[3], r ? "(fg)" : ""); }
                else if (!strcmp(sn, "SlateBrush")) { uintptr_t res = 0; char rn[48] = "-"; float c[4]; rd(a + g_res_off, &res, 8); if (res) obj_name(res, rn, sizeof rn); rd(a + g_tint_off, c, 16);
                    snprintf(buf, sizeof buf, "%s%s=[%s %.2f,%.2f,%.2f,%.2f] ", pre, pn, rn, c[0], c[1], c[2], c[3]); }
                else if (sp && depth < 2 && (ci_has(sn, "Style") || ci_has(sn, "Font"))) { char p2[100]; snprintf(p2, sizeof p2, "%s%s.", pre, pn); dump_struct(a, sp, p2, depth + 1, out, cap); }
                if (buf[0] && (int)(strlen(out) + strlen(buf)) < cap - 1) strcat(out, buf);
            }
            if (!rdp(p + 0x20, &p)) break;
        }
        uintptr_t sup = 0; rdp(st + 0x40, &sup); st = sup;
        char cn[64]; if (st && obj_name(st, cn, sizeof cn) && (!strcmp(cn, "Widget") || !strcmp(cn, "Visual"))) break;
    }
}
static void dump_named(uintptr_t o, uintptr_t cls) {               /* scoreboard and player-name widgets, part by part */
    static int n; static uintptr_t seen[256]; static int ns;
    if (n >= 70) return;
    char nm[64] = ""; if (!obj_name(o, nm, sizeof nm)) return;
    char oc0[96]; owner_name(o, oc0, sizeof oc0);
    if (!strcmp(oc0, "Scorecard_C")) goto dump_it;
    if (!(ci_has(nm, "Team1") || ci_has(nm, "Team2") || ci_has(nm, "Score") || ci_has(nm, "MatchState") || ci_has(nm, "UserName") || ci_has(nm, "PlayerName") || ci_has(nm, "Timer") || ci_has(nm, "Clock"))) return;
dump_it:;
    char oc[96]; owner_name(o, oc, sizeof oc);
    uintptr_t h = (uintptr_t)obj_class(o); for (const char *q = nm; *q; q++) h = h * 131 + (unsigned char)*q; for (const char *q = oc; *q; q++) h = h * 131 + (unsigned char)*q;
    for (int k = 0; k < ns; k++) if (seen[k] == h) return;
    if (ns < 256) seen[ns++] = h;
    char cn[64] = ""; obj_name(cls, cn, sizeof cn);
    uint8_t v = 0; if (o_w_vis >= 0) rd(o + o_w_vis, &v, 1);
    static char out[1400]; out[0] = 0; dump_struct(o, cls, "", 0, out, sizeof out);
    char sl[80] = "";
    if (!strcmp(cn, "TextBlock") && g_sw_off >= 0) { uintptr_t sw = 0; float c[4]; uint8_t r = 9; if (rdp(o + g_sw_off, &sw) && sw > 0x10000 && rd(sw + g_sc_off, c, 16)) { rd(sw + g_sc_off + 16, &r, 1); snprintf(sl, sizeof sl, " SHOWN=%.2f,%.2f,%.2f,%.2f r%d", c[0], c[1], c[2], c[3], r); } }
    int reg = 0; for (int i = 0; i < g_nlive; i++) if (g_live[i].obj == o) { reg = 1 + g_live[i].applied; break; }
    logf_("theme: part %s '%s' in %s vis %d reg %d%s: %s", cn, nm, oc, v, reg, sl, out); n++;
}
static void dump_board(uintptr_t o, uintptr_t cls) {                 /* every box of the in-match scoreboard panels, 3 s after it appears */
    static uint64_t seen[96]; static DWORD seent[96]; static uint8_t done_[96]; static int ns, n;
    if (n >= 60) return;
    char oc[96]; owner_name(o, oc, sizeof oc);
    if (strcmp(oc, "Scorecard_C") && strcmp(oc, "Team1Info_C") && strcmp(oc, "Team2Info_C") && strcmp(oc, "MatchInfo_C")) return;
    char cn[64] = "", nm[64] = ""; obj_name(cls, cn, sizeof cn); obj_name(o, nm, sizeof nm);
    if (strcmp(cn, "Image") && strcmp(cn, "Border") && strcmp(cn, "Button")) return;
    uint64_t h = (uint64_t)o ^ ((uint64_t)obj_class(o) << 20);
    int slot = -1; for (int k = 0; k < ns; k++) if (seen[k] == h) slot = k;
    if (slot < 0) { if (ns < 96) { seen[ns] = h; seent[ns] = GetTickCount(); done_[ns] = 0; ns++; } return; }
    if (done_[slot] || GetTickCount() - seent[slot] < 3000) return;
    done_[slot] = 1;
    static char out[900]; out[0] = 0; dump_struct(o, cls, "", 0, out, sizeof out);
    uint8_t v = 0; if (o_w_vis >= 0) rd(o + o_w_vis, &v, 1);
    logf_("theme: board %s '%s' in %s vis %d: %s", cn, nm, oc, v, out); n++;
}
static void dump_bytext(uintptr_t o, uintptr_t cls) {               /* texts that show a player's name: what are they? */
    static uintptr_t lcls; static int32_t toff = -1; static int n; static uint64_t seen[64]; static DWORD seent[64]; static uint8_t done_[64]; static int ns;
    if (n >= 40) return;
    if (cls != lcls) { lcls = cls; toff = -1; uintptr_t tp = find_prop(cls, "Text"); char tn[64]; if (tp && fld_type(tp, tn, sizeof tn) && !strcmp(tn, "TextProperty")) prop_off(tp, &toff); }
    if (toff < 0) return;
    uintptr_t td = 0; wchar_t w[40]; if (!rdp(o + toff, &td) || !td || !text_of(td, w, 36)) return;
    if (_wcsicmp(w, L"Na (Virginia)")) return;
    char nm[64] = "", cn[64] = "", oc[96] = ""; obj_name(o, nm, sizeof nm); obj_name(cls, cn, sizeof cn); owner_name(o, oc, sizeof oc);
    uint64_t h = 1469598103934665603ull; for (const char *q = nm; *q; q++) h = (h ^ (unsigned char)*q) * 1099511628211ull; for (const char *q = oc; *q; q++) h = (h ^ (unsigned char)*q) * 1099511628211ull;
    int slot = -1; for (int k = 0; k < ns; k++) if (seen[k] == h) slot = k;
    if (slot < 0) { if (ns < 64) { seen[ns] = h; seent[ns] = GetTickCount(); done_[ns] = 0; ns++; } return; }
    if (done_[slot] || GetTickCount() - seent[slot] < 3000) return;    /* look again 3 s later, after the theme had its turn */
    done_[slot] = 1;
    static char out[1200]; out[0] = 0; dump_struct(o, cls, "", 0, out, sizeof out);
    char sl[90] = ""; uintptr_t sw = 0; float c[4]; uint8_t r = 9;
    if (g_sw_off >= 0 && rdp(o + g_sw_off, &sw) && sw > 0x10000 && rd(sw + g_sc_off, c, 16)) { rd(sw + g_sc_off + 16, &r, 1); snprintf(sl, sizeof sl, " SHOWN=%.2f,%.2f,%.2f,%.2f r%d", c[0], c[1], c[2], c[3], r); }
    int reg = 0, ap = -1, tg = 0; for (int i = 0; i < g_nlive; i++) if (g_live[i].obj == o) { reg++; ap = g_live[i].applied; }
    for (int i = 0; i < g_ntw; i++) if (g_tw[i].obj == o) tg = 1 + g_tw[i].mode;
    uint8_t v = 0; if (o_w_vis >= 0) rd(o + o_w_vis, &v, 1);
    logf_("theme: name text %s '%s' in %s vis %d reg %d applied %d tag %d%s: %s", cn, nm, oc, v, reg, ap, tg, sl, out); n++;
}
static void dump_widget(uintptr_t o, uintptr_t cls) {             /* one settings row, part by part (to see what makes it blue) */
    static uintptr_t row; char oc[96]; owner_name(o, oc, sizeof oc);
    if (strcmp(oc, "SettingsListItem_C") || g_dumpn >= 45) return;
    uintptr_t ow = 0; rdp(o + 0x20, &ow); if (!row) row = ow; if (ow != row) return;
    char nm[64] = "", cn[64] = ""; obj_name(o, nm, sizeof nm); obj_name(cls, cn, sizeof cn);
    uint8_t v = 0; if (o_w_vis >= 0) rd(o + o_w_vis, &v, 1);
    static char out[1600]; out[0] = 0; dump_struct(o, cls, "", 0, out, sizeof out);
    logf_("theme: row part %s '%s' vis %d: %s", cn, nm, v, out); g_dumpn++;
}

/* diagnostic: the Settings "Apply ..." button, its parents and their children (logged again whenever a colour changes) */
static void dump_one(uintptr_t w, int depth, char *all, int cap) {
    char cn[64] = "", nm[64] = "", out[700] = ""; obj_name(obj_class(w), cn, sizeof cn); obj_name(w, nm, sizeof nm);
    uint8_t v = 0; if (o_w_vis >= 0) rd(w + o_w_vis, &v, 1);
    dump_struct(w, obj_class(w), "", 0, out, sizeof out);
    char line[900]; snprintf(line, sizeof line, "\n   %*s%s '%s' vis %d: %s", depth * 2, "", cn, nm, v, out);
    if ((int)(strlen(all) + strlen(line)) < cap - 1) strcat(all, line);
}
static void dump_apply(uintptr_t o) {
    static int n; static uint64_t last[8]; if (n >= 40) return;
    char root[96]; if (!chain_visible(o, root, sizeof root)) return;
    static char all[12000]; all[0] = 0;
    uintptr_t w = o;
    for (int d = 0; d < 4 && w; d++) {
        dump_one(w, 0, all, sizeof all);
        uintptr_t slot = 0, par = 0; if (!rdp(w + o_w_slot, &slot) || !slot || !rdp(slot + o_ps_parent, &par) || !par) break;
        if (o_pw_slots >= 0) { TArr sl; if (rd(par + o_pw_slots, &sl, sizeof sl) && sl.num > 0 && sl.num < 12)
            for (int i = 0; i < sl.num; i++) { uintptr_t s2 = 0, c = 0; if (rdp(sl.ptr + 8ULL * i, &s2) && s2 && rdp(s2 + o_ps_parent + 8, &c) && c && c != w) dump_one(c, 1, all, sizeof all); } }
        w = par;
    }
    uint64_t h = 1469598103934665603ull; for (const char *q = all; *q; q++) h = (h ^ (unsigned char)*q) * 1099511628211ull;
    for (int i = 0; i < 8; i++) if (last[i] == h) return;
    last[n % 8] = h; n++;
    logf_("theme: APPLY button (%s):%s", root, all);
}
static int okind_for(uintptr_t o, unsigned si) {
    (void)dump_apply;                   /* which colour rule a widget's setter follows */
    int okind = SETTERS[si].kind;
    int sck = (!strcmp(SETTERS[si].cls, "Image") && !strcmp(SETTERS[si].prop, "ColorAndOpacity")) || (!strcmp(SETTERS[si].cls, "Border") && !strcmp(SETTERS[si].prop, "BrushColor")) ? owner_is_score(o) : 0;
    if (sck) okind = sck == 1 ? CK_SCORE : sck == 2 ? CK_SCORE_K : CK_SCORE_D;   /* in-match scoreboard */
    else if (okind == CK_MUL || (okind == CK_BG && strcmp(SETTERS[si].cls, "Button"))) {      /* hidden until hovered/selected: a highlight -> inverted */
        uint8_t v = 0; if (o_w_vis >= 0 && rd(o + o_w_vis, &v, 1) && (v == 1 || v == 2)) okind = CK_HILITE;
        if (!strcmp(SETTERS[si].cls, "Image")) { int cs = si_of("Image", "ColorAndOpacity"); float ca[4];
            if (cs >= 0 && rd(o + g_set[cs].off, ca, 16) && ca[3] < 0.05f) okind = CK_HILITE;      /* invisible until the game shows it */
            else if (!strcmp(SETTERS[si].prop, "ColorAndOpacity") && img_res(o)) okind = CK_HILITE; }    /* blue tint over an icon/picture -> white, other tints kept */
    }
    return okind;
}
/* widgets born from a darkened blueprint copy: put the copy's real colours into the recorded original (for Sun) */
typedef struct { uintptr_t buf, o; int32_t base; } RestCtx;
static void rest_cb(void *ctx, uintptr_t a, int kind, int fc) {
    RestCtx *r = (RestCtx *)ctx; float cur[4], oc[4], od[4];
    int objoff = r->base + (int)(a - r->buf);
    if (!col_read(a, kind, fc, cur) || !torig_get(tpl_key(r->o, 0, 0x10000 + objoff), oc)) return;
    if (col_new(kind & 0xFF, oc, od) && fabsf(od[0] - cur[0]) < 0.01f && fabsf(od[1] - cur[1]) < 0.01f && fabsf(od[2] - cur[2]) < 0.01f && fabsf(od[3] - cur[3]) < 0.01f) col_write(a, fc, oc);
}
static int th_scan_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    ThScan *s = (ThScan *)ctx; char nm[96];
    if (cls == g_cls_world) { if (obj_name(o, nm, sizeof nm)) { if (!strcmp(nm, "MainMenu")) s->worlds_menu++; else if (!strncmp(nm, "MP_", 3)) s->worlds_match++; } return 0; }
    if (cls == g_cls_gi && o_gi_userid >= 0 && !is_cdo(o)) { char id[64]; g_gi_obj = o; if (get_fstring(o, o_gi_userid, id, sizeof id) && id[0]) s->logged = 1; return 0; }
    int k = widget_kind(cls);
    if (k < 0) return 0;
    uint32_t fl = 0; rd(o + 8, &fl, 4);
    int is_tpl = (fl & 0x30) != 0;                                  /* class default / archetype = template */
    if (!is_tpl) {                                                   /* a widget inside a widget class's own tree is a template too */
        uintptr_t ow = 0, ow2 = 0; char cn[64];
        if (rdp(o + 0x20, &ow) && ow && obj_name(obj_class(ow), cn, sizeof cn) && !strcmp(cn, "WidgetTree") && rdp(ow + 0x20, &ow2) && ow2
            && obj_name(obj_class(ow2), cn, sizeof cn) && (!strcmp(cn, "WidgetBlueprintGeneratedClass") || !strcmp(cn, "Class"))) is_tpl = 1;
    }
    if (is_tpl) {
        { int32_t ix = -1, sr = 0; rd(o + 0x0C, &ix, 4); uintptr_t itx = obj_item_rd(ix); if (itx) rd(itx + 0x10, &sr, 4); g_tplown = o; g_tplown_idx = ix; g_tplown_ser = sr; }
        if (k < (int)NSET) {                                         /* blueprint copies of texts/sliders/icons: new widgets are born themed */
            const char *c = SETTERS[k].cls; int a1 = -1, a2 = -1, kk = 0;
            if ((!strcmp(c, "Border") || !strcmp(c, "Button") || !strcmp(c, "Image")) && !seen_tpl(o ^ 0x77)) {   /* backgrounds, buttons, pictures */
                int before = g_ntpl; g_tplcur = o;
                for (unsigned si = 0; si < NSET; si++) {
                    if (strcmp(SETTERS[si].cls, c) || g_set[si].off < 0) continue;
                    if (!strcmp(c, "Image") && !strcmp(SETTERS[si].prop, "ColorAndOpacity")) continue;      /* done below */
                    int ok = okind_for(o, si); uintptr_t a = o + g_set[si].off;
                    if (!strcmp(c, "Image") && !strcmp(SETTERS[si].prop, "Brush")) {
                        if (img_is_picture_now(o) || rs_match(a)) continue;                                   /* pictures and swapped icons */
                        if (ok == CK_BG && img_is_icon(o)) ok = CK_TEXT;
                    }
                    if (g_set[si].is_struct) walk_struct(a, g_set[si].ustruct, ok, 0, tpl_cb, NULL, ok == CK_TEXT);
                    else tpl_cb(NULL, a, ok | (g_set[si].size >= 32 ? 0x100 : 0), 0);
                }
                g_tplcur = 0;
                if (g_dark) for (int i = before; i < g_ntpl; i++) col_write(g_tpl[i].addr, g_tpl[i].is_fcolor, g_tpl[i].dark);
                s->new_tpl += g_ntpl - before;
            }
            if (!strcmp(c, "TextBlock")) { a1 = si_of("TextBlock", "ColorAndOpacity"); kk = CK_TEXT | 0x100; }
            else if (!strcmp(c, "Slider")) { a1 = si_of("Slider", "SliderBarColor"); a2 = si_of("Slider", "SliderHandleColor"); kk = CK_TEXT; }
            else if (!strcmp(c, "Image")) { a1 = si_of("Image", "ColorAndOpacity"); kk = CK_HILITE; }
            else if (!strcmp(c, "RichTextBlock")) {
                int rs = si_of("RichTextBlock", "DefaultTextStyleOverride"); int32_t co = -1;
                if (rs < 0 || g_set[rs].off < 0 || !g_set[rs].ustruct || !prop_off(find_prop(g_set[rs].ustruct, "ColorAndOpacity"), &co)) return 0;
                if (seen_tpl(o)) return 0;
                int before = g_ntpl; tpl_cb(NULL, o + g_set[rs].off + co, CK_TEXT | 0x100, 0);
                if (g_dark) for (int i = before; i < g_ntpl; i++) col_write(g_tpl[i].addr, g_tpl[i].is_fcolor, g_tpl[i].dark);
                s->new_tpl += g_ntpl - before;
                return 0;
            }
            else return 0;
            if (seen_tpl(o)) return 0;
            if (!strcmp(c, "Slider") && slider_excluded(o)) return 0;
            int before = g_ntpl;
            if (a1 >= 0 && g_set[a1].off >= 0) { float oc4[4]; if (rd(o + g_set[a1].off, oc4, 16)) torig_put(tpl_key(o, 1, a1), oc4); }
            if (a2 >= 0 && g_set[a2].off >= 0) { float oc4[4]; if (rd(o + g_set[a2].off, oc4, 16)) torig_put(tpl_key(o, 1, a2), oc4); }
            if (a1 >= 0 && g_set[a1].off >= 0) tpl_cb(NULL, o + g_set[a1].off, kk, 0);
            if (a2 >= 0 && g_set[a2].off >= 0) tpl_cb(NULL, o + g_set[a2].off, kk, 0);
            if (g_dark) for (int i = before; i < g_ntpl; i++) col_write(g_tpl[i].addr, g_tpl[i].is_fcolor, g_tpl[i].dark);
            s->new_tpl += g_ntpl - before;
            return 0;
        }
        if (seen_tpl(o)) return 0;
        if (k < (int)NSET && !strcmp(SETTERS[k].cls, "Image")) { int32_t bo = -1; for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].prop, "Brush") && !strcmp(SETTERS[i].cls, "Image")) bo = g_set[i].off; if (bo >= 0 && img_has_picture(o, bo)) return 0; }
        int before = g_ntpl;
        int tk = 0; obj_name(cls, nm, sizeof nm); if (ci_has(nm, "Text")) tk = 1;
        { int32_t ix = -1, sr = 0; rd(o + 0x0C, &ix, 4); uintptr_t it2 = obj_item_rd(ix); if (it2) rd(it2 + 0x10, &sr, 4); g_rscur.obj = o; g_rscur.idx = ix; g_rscur.ser = sr; }
        g_tplcur = o; walk_struct(o, cls, class_kind_for(nm), 0, tpl_cb, NULL, tk); g_tplcur = 0;      /* checkbox textures: swapped on live widgets only */
        if (g_dark) for (int i = before; i < g_ntpl; i++) col_write(g_tpl[i].addr, g_tpl[i].is_fcolor, g_tpl[i].dark);
        s->new_tpl += g_ntpl - before;
        return 0;
    }
    if (k < (int)NSET && !strcmp(SETTERS[k].cls, "TextBlock") && o_tb_text >= 0 && o_w_slot >= 0 && o_ps_parent >= 0) {
        uintptr_t td = 0; wchar_t w[24];
        if (rdp(o + o_tb_text, &td) && text_of(td, w, 20) && !_wcsicmp(w, L"Exit")) {
            char root[96];
            if (chain_visible(o, root, sizeof root) && !strcmp(root, "MainMenu_C")) {
                s->exit_vis++; snprintf(g_exit_root, sizeof g_exit_root, "%s", root);
                int32_t idx = -1, ser = 0; rd(o + 0x0C, &idx, 4); uintptr_t it = obj_item_rd(idx);
                if (it && rd(it + 0x10, &ser, 4)) { g_exit_idx = idx; g_exit_ser = ser; g_exit_obj = o; }
            }
        }
    }
    if (k == 99 && o_w_slot >= 0) {                                   /* user widgets (whole menus) */
        static uintptr_t uwc; char cn[96];
        if (!uwc) { uintptr_t c = cls; for (int d = 0; c && d < 12; d++) { if (obj_name(c, cn, sizeof cn) && !strcmp(cn, "UserWidget")) { uwc = c; break; } uintptr_t sp = 0; rdp(c + 0x40, &sp); c = sp; } }
        uintptr_t c = cls; int isuw = 0; for (int d = 0; c && d < 12; d++) { if (c == uwc) { isuw = 1; break; } uintptr_t sp = 0; rdp(c + 0x40, &sp); c = sp; }
        if (isuw && g_dark) ufg_note(o, uwc);
        if (isuw && obj_name(cls, cn, sizeof cn)) {
            char root[96]; uintptr_t slot = 0; rdp(o + o_w_slot, &slot);
            if (chain_visible(o, root, sizeof root)) {
                if (strlen(s->vis) + strlen(cn) < 580 && !strstr(s->vis, cn)) { strcat(s->vis, cn); strcat(s->vis, " "); }
                if ((ci_has(cn, "PlayerCustomization") || ci_has(cn, "Customiz") || ci_has(cn, "Appearance") || ci_has(cn, "Creator") || ci_has(cn, "MyPlayer") || ci_has(cn, "Settings") || ci_has(cn, "MyTeam"))) s->overlay++;
            }
        }
    }
    if (k >= (int)NSET) { static uintptr_t ccls; if (!ccls) { char ccn[48]; if (obj_name(cls, ccn, sizeof ccn) && !strcmp(ccn, "ComboBoxString")) ccls = cls; } if (cls == ccls) combo_note(o); }
    if (k >= (int)NSET) {                                            /* live widget without setters */
        int32_t idx = -1, ser = 0; rd(o + 0x0C, &idx, 4);
        uintptr_t it = obj_item_rd(idx), chk = 0;
        if (!it || !rdp(it, &chk) || chk != o || !rd(it + 0x10, &ser, 4)) return 0;
        if (seen_tpl(inst_key(o))) return 0;
        char cn[96] = ""; obj_name(cls, cn, sizeof cn);
        g_ipcur.obj = o; g_ipcur.idx = idx; g_ipcur.serial = ser;
        g_rscur.obj = o; g_rscur.idx = idx; g_rscur.ser = ser;
        g_rs_on = 1; walk_struct(o, cls, class_kind_for(cn), 0, inst_cb, NULL, ci_has(cn, "Text") && !ci_has(cn, "Box")); g_rs_on = 0;
        return 0;
    }
    (void)dump_widget; (void)dump_named; (void)dump_bytext; (void)dump_board;
    /* live widget with setters: one entry per setter */
    int32_t idx = -1, ser = 0; rd(o + 0x0C, &idx, 4);
    uintptr_t it = obj_item_rd(idx), chk = 0;
    if (!it || !rdp(it, &chk) || chk != o || !rd(it + 0x10, &ser, 4)) return 0;
    if ((!strcmp(SETTERS[k].cls, "Button") || !strcmp(SETTERS[k].cls, "Image")) && !seen_tpl(inst_key(o) ^ 0x5)) {   /* buttons drawn from pictures (back button) */
        g_rscur.obj = o; g_rscur.idx = idx; g_rscur.ser = ser;
        g_rs_on = 1; walk_struct(o, cls, CK_BG, 0, rs_noop_cb, NULL, 0); g_rs_on = 0;
    }
    if (!strcmp(SETTERS[k].cls, "Slider") && !seen_tpl(inst_key(o) ^ 0x7) && !slider_excluded(o)) {   /* slider handle/bar pictures */
        uintptr_t sp = find_prop(cls, "WidgetStyle"), sst = 0; int32_t so = -1;
        if (sp && prop_off(sp, &so) && rdp(sp + 0x78, &sst) && sst) {      /* only the style pictures; the two colours go through the game's setters */
            g_ipcur.obj = o; g_ipcur.idx = idx; g_ipcur.serial = ser;
            walk_struct(o + so, sst, CK_TEXT, 0, inst_cb, NULL, 1);
        }
    }
    for (unsigned si = 0; si < NSET; si++) {
        if (strcmp(SETTERS[si].cls, SETTERS[k].cls) || !g_set[si].fn || g_set[si].off < 0) continue;
        if (g_nlive >= MAXLIVE || live_seen(inst_key(o), 0, (int)si)) continue;
        if (!strcmp(SETTERS[si].cls, "Border")) { int32_t bo = -1; for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "Border") && !strcmp(SETTERS[i].prop, "Brush")) bo = g_set[i].off; if (bo >= 0 && (brush_is_photo(o, bo) || brush_is_picture_at(o + bo))) continue; }
        if (!strcmp(SETTERS[si].cls, "Image") && !strcmp(SETTERS[si].prop, "Brush") && img_is_picture_now(o)) continue;
        if (!strcmp(SETTERS[si].cls, "Image") && !strcmp(SETTERS[si].prop, "Brush") && owner_is_score(o)) continue;   /* scoreboard: only its tint is changed */
        if (!strcmp(SETTERS[si].cls, "Slider") && slider_excluded(o)) continue;     /* the music player's volume slider keeps its own look */
        if (0) { int32_t bo = -1; for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "Image") && !strcmp(SETTERS[i].prop, "Brush")) bo = g_set[i].off; if (bo >= 0 && img_has_picture(o, bo)) continue; }
        int okind = okind_for(o, si);
        uint8_t *orig = malloc(g_set[si].size + 64);
        if (!orig || !rd(o + g_set[si].off, orig, g_set[si].size)) { free(orig); continue; }
        { float c0[4]; memcpy(c0, orig, 16); float tc[4];
          if (g_dark && is_ourwhite(c0) && torig_get(tpl_key(o, 0, (int)si), tc)) { memcpy(orig, tc, 16); okind = okind ? okind : SETTERS[si].kind; } }
        if (g_dark && g_torig) {                                      /* born from a darkened copy: the copy's real colours are the originals */
            RestCtx rc = { (uintptr_t)orig, o, g_set[si].off };
            if (g_set[si].is_struct) walk_struct((uintptr_t)orig, g_set[si].ustruct, okind, 0, rest_cb, &rc, okind == CK_TEXT);
            else rest_cb(&rc, (uintptr_t)orig, okind | (g_set[si].size >= 32 ? 0x100 : 0), 0);
        }
        /* skip if dark mode would change nothing */
        int changes = 0;
        if (g_set[si].is_struct) { static uint8_t tmp[2048]; memcpy(tmp, orig, g_set[si].size); DualCtx d = { 1, (uintptr_t)tmp, (uintptr_t)orig }; g_tint_delta = 0; walk_struct((uintptr_t)tmp, g_set[si].ustruct, okind, 0, copy_cb, &d, okind == CK_TEXT); if (!strcmp(SETTERS[si].cls, "Image")) walk_struct((uintptr_t)tmp, g_set[si].ustruct, CK_TEXT, 0, copy_cb, &d, 1); changes = memcmp(tmp, orig, g_set[si].size) != 0; }
        else { float c[4], n[4]; memcpy(c, orig, 16); changes = !(g_set[si].size >= 32 && orig[16]) && col_new(okind, c, n); }
        if (!changes && strcmp(SETTERS[si].cls, "TextBlock")) { free(orig); continue; }
        EnterCriticalSection(&g_thcs);
        LiveW *w = &g_live[g_nlive]; w->obj = o; w->idx = idx; w->serial = ser; w->si = (uint8_t)si; w->applied = th_skip(o) ? 3 : 0; w->tag = 0; w->kind = (uint8_t)(okind != SETTERS[si].kind ? okind : 0); w->orig = orig; g_nlive++;
        LeaveCriticalSection(&g_thcs);
    }
    return 0;
}
static int th_setup_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; char nm[96], on[96] = "";
    if ((class_is(cls, "Class") || class_is(cls, "BlueprintGeneratedClass")) && obj_name(o, nm, sizeof nm)) {
        if (!strcmp(nm, "World")) g_cls_world = o;
        else if (!strcmp(nm, "PSO_GameInstance_C")) g_cls_gi = o;
        else if (!strcmp(nm, "Widget")) g_cls_widget = o;
        else if (!strcmp(nm, "WidgetSwitcher")) g_cls_switcher = o;
        else if (!strcmp(nm, "PanelSlot")) g_cls_panelslot = o;
        else if (!strcmp(nm, "PanelWidget")) g_cls_panelw = o;
        else if (!strcmp(nm, "UserWidget")) g_cls_uw = o;
        else if (!strcmp(nm, "Image")) g_cls_img = o;
        else if (!strcmp(nm, "WidgetTree")) g_cls_wtree = o;
        for (unsigned i = 0; i < NSET; i++) if (!strcmp(nm, SETTERS[i].cls)) g_set[i].cls = o;
    } else if (!g_krl_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__KismetRenderingLibrary")) g_krl_cdo = o;
    else if (!g_sbl_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__SlateBlueprintLibrary")) g_sbl_cdo = o;
    else if (!g_wbl_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__WidgetBlueprintLibrary")) g_wbl_cdo = o;
    else if (!g_gs_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__GameplayStatics")) g_gs_cdo = o;
    else if (!g_wll_cdo && obj_name(o, nm, sizeof nm) && !strcmp(nm, "Default__WidgetLayoutLibrary")) g_wll_cdo = o;
    else if (class_is(cls, "Function") && obj_name(o, nm, sizeof nm)) {
        uintptr_t outer = 0; if (rdp(o + 0x20, &outer)) obj_name(outer, on, sizeof on);
        if (!strcmp(nm, "ImportBufferAsTexture2D") && !strcmp(on, "KismetRenderingLibrary")) g_fn_import = o;
        else if (!strcmp(nm, "GetCachedGeometry") && !strcmp(on, "Widget")) g_fn_cgeo = o;
        else if (!strcmp(nm, "LocalToAbsolute") && !strcmp(on, "SlateBlueprintLibrary")) g_fn_l2a = o;
        else if (!strcmp(nm, "GetLocalSize") && !strcmp(on, "SlateBlueprintLibrary")) g_fn_lsize = o;
        for (int f = 0; f < F_N; f++) if (!FNS[f].fn && !strcmp(nm, FNS[f].name) && !strcmp(on, FNS[f].outer)) FNS[f].fn = o;
        for (unsigned i = 0; i < NSET; i++) if (!strcmp(on, SETTERS[i].cls) && !strcmp(nm, SETTERS[i].setter)) g_set[i].fn = o;
    }
    return 0;
}
static void setters_resolve(void) {
    char tn[64], sn[96];
    for (unsigned i = 0; i < NSET; i++) {
        SetterRT *s = &g_set[i]; s->off = -1;
        if (!s->cls) continue;
        s->prop = find_prop(s->cls, SETTERS[i].prop);
        if (!s->prop || !rd(s->prop + 0x4C, &s->off, 4) || !rd(s->prop + 0x3C, &s->size, 4)) { s->off = -1; continue; }
        s->is_struct = 0; sn[0] = 0;
        if (fld_type(s->prop, tn, sizeof tn) && !strcmp(tn, "StructProperty") && rdp(s->prop + 0x78, &s->ustruct) && s->ustruct) {
            obj_name(s->ustruct, sn, sizeof sn);
            s->is_struct = strcmp(sn, "LinearColor") && strcmp(sn, "SlateColor");
            if (!strcmp(sn, "SlateBrush") && g_res_off < 0) { int32_t ro = -1, to = -1; if (prop_off(find_prop(s->ustruct, "ResourceObject"), &ro)) g_res_off = ro; if (prop_off(find_prop(s->ustruct, "TintColor"), &to)) g_tint_off = to; { int32_t io = -1; if (prop_off(find_prop(s->ustruct, "ImageSize"), &io)) g_isz_off = io; }
                { int32_t it_ = -1; if (prop_off(find_prop(s->ustruct, "ImageType"), &it_)) g_rh_off = (it_ + 1 + 7) & ~7; g_brush_struct = s->ustruct; logf_("theme: brush handle @%d, size @%d", g_rh_off, g_isz_off); } logf_("theme: brush texture @%d tint @%d", g_res_off, g_tint_off); }
        }
    }
}
static void theme_save(void) { FILE *f = _wfopen(g_theme_file, L"w"); if (f) { fputs(g_dark ? "moon\n" : "sun\n", f); fclose(f); } }

/* ---- the switch (a small window over the game's top-right corner, main menu only) */
typedef struct { UINT32 v; void *cb; BOOL a, b; } TgGpIn;
int WINAPI GdiplusStartup(ULONG_PTR *, const TgGpIn *, void *);
int WINAPI GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, void **);
int WINAPI GdipGetImageGraphicsContext(void *, void **);
int WINAPI GdipCreateHBITMAPFromBitmap(void *, HBITMAP *, DWORD);
int WINAPI GdipDisposeImage(void *);
int WINAPI GdipDeleteGraphics(void *);
int WINAPI GdipSetSmoothingMode(void *, int);
int WINAPI GdipCreatePath(int, void **);
int WINAPI GdipDeletePath(void *);
int WINAPI GdipAddPathArc(void *, float, float, float, float, float, float);
int WINAPI GdipClosePathFigure(void *);
int WINAPI GdipCreateSolidFill(DWORD, void **);
int WINAPI GdipDeleteBrush(void *);
int WINAPI GdipFillPath(void *, void *, void *);
int WINAPI GdipCreatePen1(DWORD, float, int, void **);
int WINAPI GdipDeletePen(void *);
int WINAPI GdipDrawPath(void *, void *, void *);
int WINAPI GdipFillEllipse(void *, void *, float, float, float, float);
int WINAPI GdipDrawEllipse(void *, void *, float, float, float, float);
int WINAPI GdipDrawLine(void *, void *, float, float, float, float);
int WINAPI GdipSetPenStartCap(void *, int);
int WINAPI GdipSetPenEndCap(void *, int);
int WINAPI GdipSetCompositingMode(void *, int);
static HBITMAP g_tg_hb; static int g_tg_bw, g_tg_bh;
static void tg_pill(void *G, float x, float y, float w, float h, DWORD fill, DWORD line, float lw) {
    void *p, *b, *pen; float r = h / 2;
    GdipCreatePath(0, &p);
    GdipAddPathArc(p, x, y, h, h, 90, 180); GdipAddPathArc(p, x + w - h, y, h, h, 270, 180); GdipClosePathFigure(p);
    GdipCreateSolidFill(fill, &b); GdipFillPath(G, b, p); GdipDeleteBrush(b);
    if (lw > 0) { GdipCreatePen1(line, lw, 2, &pen); GdipDrawPath(G, pen, p); GdipDeletePen(pen); }
    GdipDeletePath(p); (void)r;
}
static void *tg_draw(int W, int H, int dark, BYTE *scan0) {     /* the switch picture (GDI+ bitmap, ARGB) */
    static int gp; if (!gp) { ULONG_PTR tok; TgGpIn in = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&tok, &in, NULL); gp = 1; }
    void *bmp = NULL, *G = NULL;
    if (GdipCreateBitmapFromScan0(W, H, scan0 ? W * 4 : 0, 0x26200A /* PixelFormat32bppARGB */, scan0, &bmp) || !bmp) return NULL;
    GdipGetImageGraphicsContext(bmp, &G); GdipSetSmoothingMode(G, 4);
    float s = H / 30.0f, lw = 1.4f * s;
    DWORD navy = 0xFF14148C;                                   /* the light UI's blue */
    if (!dark) tg_pill(G, lw, lw, W - 2 * lw, H - 2 * lw, 0xFFE9EDF3, 0xFF9AA4B4, lw);
    else       tg_pill(G, lw, lw, W - 2 * lw, H - 2 * lw, 0xFF16181E, 0xFF5A6070, lw);
    float d = H - 6 * s, ky = 3 * s, kx = dark ? W - d - 3 * s : 3 * s, cx = kx + d / 2, cy = ky + d / 2;
    void *b, *pen;
    /* knob: a circle with an outline, the icon inside */
    GdipCreateSolidFill(dark ? 0xFF2A2E38 : 0xFFFFFFFF, &b); GdipFillEllipse(G, b, kx, ky, d, d); GdipDeleteBrush(b);
    GdipCreatePen1(dark ? 0xFFB8BECB : navy, lw, 2, &pen); GdipDrawEllipse(G, pen, kx, ky, d, d); GdipDeletePen(pen);
    if (!dark) {
        GdipCreatePen1(navy, 1.6f * s, 2, &pen); GdipSetPenStartCap(pen, 2); GdipSetPenEndCap(pen, 2);
        for (int k = 0; k < 8; k++) { float a = k * 3.14159f / 4; GdipDrawLine(G, pen, cx + cosf(a) * d * 0.24f, cy + sinf(a) * d * 0.24f, cx + cosf(a) * d * 0.36f, cy + sinf(a) * d * 0.36f); }
        GdipDeletePen(pen);
        GdipCreateSolidFill(navy, &b); GdipFillEllipse(G, b, cx - d * 0.15f, cy - d * 0.15f, d * 0.30f, d * 0.30f); GdipDeleteBrush(b);
    } else {
        float m = d * 0.56f;
        GdipCreateSolidFill(0xFFF2F4F8, &b); GdipFillEllipse(G, b, cx - m / 2, cy - m / 2, m, m); GdipDeleteBrush(b);
        GdipCreateSolidFill(0xFF2A2E38, &b); GdipFillEllipse(G, b, cx - m / 2 + m * 0.36f, cy - m / 2 - m * 0.10f, m * 0.92f, m * 0.92f); GdipDeleteBrush(b);
    }
    GdipDeleteGraphics(G);
    return bmp;
}
static volatile int g_ig_ok;                                           /* the switch is drawn inside the game */
static void tg_render(HWND h, int W, int H) {
    HBITMAP hb = NULL;
    if (g_ig_ok) {                                                     /* window only catches the click: nearly invisible */
        BITMAPINFO bi = {0}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; void *bits = NULL;
        hb = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hb && bits) for (int i = 0; i < W * H; i++) ((DWORD *)bits)[i] = 0x01000000;
    } else {
        void *bmp = tg_draw(W, H, g_dark, NULL); if (!bmp) return;
        GdipCreateHBITMAPFromBitmap(bmp, &hb, 0); GdipDisposeImage(bmp);
    }
    if (!hb) return;
    if (g_tg_hb) DeleteObject(g_tg_hb);
    g_tg_hb = hb; g_tg_bw = W; g_tg_bh = H; (void)h;
}
static void tg_present(HWND h, int x, int y, int clip, BYTE alpha) {   /* clip = rows hidden at the top */
    if (!g_tg_hb || clip >= g_tg_bh) return;
    HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc); HGDIOBJ old = SelectObject(mdc, g_tg_hb);
    SIZE sz = { g_tg_bw, g_tg_bh - clip }; POINT src = { 0, clip }, dst = { x, y + clip }; BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(h, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, old); DeleteDC(mdc); ReleaseDC(NULL, sdc);
}
static int g_tg_w, g_tg_h, g_tg_x, g_tg_y, g_tg_clip; static BYTE g_tg_alpha = 255;
static LRESULT CALLBACK tg_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONUP: g_dark = !g_dark; if (g_tg_w) { tg_render(h, g_tg_w, g_tg_h); tg_present(h, g_tg_x, g_tg_y, g_tg_clip, g_tg_alpha); } if (g_sub_hwnd) PostMessageW(g_sub_hwnd, WM_THEME_NOW, 0, 0); theme_save(); return 0;
    case WM_SETCURSOR: SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}
static HWND g_game_hwnd;
static BOOL CALLBACK th_wnd_cb(HWND h, LPARAM l) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); wchar_t c[64];
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetClassNameW(h, c, 64) && !wcscmp(c, L"UnrealWindow")) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}
#define WM_THEME_POS (WM_APP + 0x3A2)
#define WM_THEME_TICK (WM_APP + 0x3A3)
static volatile LONG g_tick_pending;
static volatile LONG g_pos_pending, g_ex_seq; static volatile int g_pos_ok = -1; static volatile float g_ex_box[4], g_ex_op = 1; static volatile DWORD g_ex_tick;
static float chain_opacity(uintptr_t w) {
    float op = 1;
    for (int d = 0; w && d < 48 && o_w_opac >= 0; d++) {
        float v = 1; if (rd(w + o_w_opac, &v, 4) && v >= 0 && v <= 1) op *= v;
        uintptr_t slot = 0; rdp(w + o_w_slot, &slot);
        if (slot) { uintptr_t par = 0; if (!rdp(slot + o_ps_parent, &par) || !par) break; w = par; continue; }
        uintptr_t outer = 0; char on[64];
        if (rdp(w + 0x20, &outer) && outer && obj_name(obj_class(outer), on, sizeof on) && !strcmp(on, "WidgetTree")) { uintptr_t uw = 0; if (!rdp(outer + 0x20, &uw) || !uw) break; w = uw; continue; }
        break;
    }
    return op;
}
static void pos_query(void) {                                       /* game thread: where is Exit drawn this frame */
    static int32_t r_geo = -1, gsz = 0, l_geo = -1, l_ret = -1, a_geo = -1, a_loc = -1, a_ret = -1, ok = -1;
    if (ok < 0) {
        ok = 0; uintptr_t p;
        if (g_fn_cgeo && g_fn_l2a && g_fn_lsize && g_sbl_cdo) {
            p = find_prop(g_fn_cgeo, "ReturnValue"); if (prop_off(p, &r_geo)) rd(p + 0x3C, &gsz, 4);
            prop_off(find_prop(g_fn_lsize, "Geometry"), &l_geo); prop_off(find_prop(g_fn_lsize, "ReturnValue"), &l_ret);
            prop_off(find_prop(g_fn_l2a, "Geometry"), &a_geo); prop_off(find_prop(g_fn_l2a, "LocalCoordinate"), &a_loc); prop_off(find_prop(g_fn_l2a, "ReturnValue"), &a_ret);
            ok = r_geo >= 0 && gsz > 0 && gsz < 200 && l_geo >= 0 && l_ret >= 0 && a_geo >= 0 && a_loc >= 0 && a_ret >= 0;
        }
        g_pos_ok = ok; logf_("theme: switch follows the menu: %s", ok ? "yes" : "no (fixed spot)");
    }
    if (!ok) return;
    uintptr_t t = g_exit_obj; if (!t) return;
    uintptr_t it = obj_item_rd(g_exit_idx), chk = 0; int32_t ser = 0;
    if (!it || !rdp(it, &chk) || chk != t || !rd(it + 0x10, &ser, 4) || ser != g_exit_ser) return;
    uintptr_t o = t, slot = 0, par = 0;                              /* the Exit button around the text */
    if (rdp(t + o_w_slot, &slot) && slot && rdp(slot + o_ps_parent, &par) && par) o = par;
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE);
    static uint8_t P[512], Q[512]; uint8_t geo[200];
    memset(P, 0, sizeof P); upe((void *)o, (void *)g_fn_cgeo, P); memcpy(geo, P + r_geo, gsz);
    memset(Q, 0, sizeof Q); memcpy(Q + l_geo, geo, gsz); upe((void *)g_sbl_cdo, (void *)g_fn_lsize, Q);
    float sz[2]; memcpy(sz, Q + l_ret, 8);
    if (!(sz[0] > 1 && sz[1] > 1 && sz[0] < 10000)) return;
    float box[4];
    for (int k = 0; k < 2; k++) {
        float loc[2] = { k ? sz[0] : 0, k ? sz[1] : 0 };
        memset(Q, 0, sizeof Q); memcpy(Q + a_geo, geo, gsz); memcpy(Q + a_loc, loc, 8);
        upe((void *)g_sbl_cdo, (void *)g_fn_l2a, Q); memcpy(box + 2 * k, Q + a_ret, 8);
    }
    for (int k = 0; k < 4; k++) g_ex_box[k] = box[k];
    g_ex_op = chain_opacity(o); g_ex_tick = GetTickCount(); InterlockedIncrement(&g_ex_seq);
    static int lg; if (lg++ < 3) logf_("theme: Exit button at %.0f,%.0f - %.0f,%.0f, opacity %.2f", box[0], box[1], box[2], box[3], g_ex_op);
}
/* ---- the switch as part of the game's own interface (shows in real full screen too) */
static volatile int g_sw_cand, g_sw_want;                             /* set by the switch thread */
static struct { uintptr_t uw, img; int32_t ui, us, ii, is; int vis, dark, tw, th; float x, y, w, h, op; } IG;
static uintptr_t g_swt[2]; static int g_swt_w, g_swt_h, g_ig_fail;
static int obj_alive3(uintptr_t o, int32_t idx, int32_t ser) {     /* same object still in its slot (serial may be handed out later) */
    uintptr_t it = obj_item_rd(idx), chk = 0; int32_t s2 = 0, fl = 0;
    if (!(o && it && rdp(it, &chk) && chk == o && rd(it + 8, &fl, 4) && !(fl & 0x30000000))) return 0;
    return !ser || !rd(it + 0x10, &s2, 4) || s2 == ser || s2 != 0;
}
static void obj_id(uintptr_t o, int32_t *idx, int32_t *ser) { *idx = -1; *ser = 0; rd(o + 0x0C, idx, 4); uintptr_t it = obj_item_rd(*idx); if (it) rd(it + 0x10, ser, 4); }
static uintptr_t png_texture(void *ctx, const uint8_t *rgba, int W, int H) {
    static uint8_t png[400 * 200 * 4 + 400 + 4096];
    if (W * H > 400 * 200) return 0;
    int n = make_png(rgba, W, H, png); if (n <= 0) return 0;
    int ow = -1, ob = -1, orr = -1; if (!prop_off(find_prop(g_fn_import, "WorldContextObject"), &ow) || !prop_off(find_prop(g_fn_import, "Buffer"), &ob) || !prop_off(find_prop(g_fn_import, "ReturnValue"), &orr)) return 0;
    uint8_t P[256]; memset(P, 0, sizeof P); *(uintptr_t *)(P + ow) = (uintptr_t)ctx;
    struct { void *data; int32_t num, max; } arr = { png, n, n }; memcpy(P + ob, &arr, 16);
    ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)g_krl_cdo, (void *)g_fn_import, P);
    uintptr_t t = *(uintptr_t *)(P + orr);
    if (t) { int32_t idx = -1; rd(t + 0x0C, &idx, 4); uintptr_t it = obj_item_rd(idx); int32_t fl = 0; if (it && rd(it + 8, &fl, 4)) { fl |= 0x40000000; wr_(it + 8, &fl, 4); } }
    return t;
}
static int sw_textures(void *ctx, int W, int H) {
    if (g_swt[0] && g_swt[1] && W == g_swt_w && H == g_swt_h) return 1;
    if (W < 8 || H < 8 || W > 400 || H > 200 || !g_fn_import || !g_krl_cdo) return 0;
    static uint8_t buf[400 * 200 * 4];
    for (int d = 0; d < 2; d++) {
        memset(buf, 0, (size_t)W * H * 4);
        void *bmp = tg_draw(W, H, d, buf); if (!bmp) return 0; GdipDisposeImage(bmp);
        for (int i = 0; i < W * H; i++) { uint8_t t = buf[i * 4]; buf[i * 4] = buf[i * 4 + 2]; buf[i * 4 + 2] = t; }   /* BGRA -> RGBA */
        g_swt[d] = png_texture(ctx, buf, W, H);
        if (!g_swt[d]) return 0;
    }
    g_swt_w = W; g_swt_h = H; IG.dark = -1;
    return 1;
}
static void sw_layout(const float *box, int p0x, int p0y, int cw, float *x, float *y, float *W, float *H) {
    float bh = box[3] - box[1]; *H = floorf(bh * 0.9f + .5f); if (*H < 12) *H = 12;
    *W = floorf(*H * 2 + *H / 6);
    *x = floorf(box[2] + bh * 0.35f + .5f); *y = floorf((box[1] + box[3]) / 2 - *H / 2 + .5f);
    if (*x + *W > p0x + cw - 2) *x = p0x + cw - *W - 2;
    (void)p0y;
}
static void ig_hide(void) {
    if (IG.vis && obj_alive3(IG.uw, IG.ui, IG.us)) { uint8_t P[64] = {0}; uint8_t v = 1; fset(P, F_VIS, "InVisibility", &v, 1); fcall(IG.uw, F_VIS, P); }
    IG.vis = 0;
}
static void ig_update(void) {                                           /* game thread, every frame or so */
    if (g_shutdown) return;
    if (g_ig_fail > 3 || !fns_ok(F_CREATE, F_VPSCALE) || !g_wbl_cdo || !g_gs_cdo || !g_wll_cdo || !g_cls_uw || !g_cls_img || !g_cls_wtree) { if (g_ig_ok) g_ig_ok = 0; return; }
    if (!g_sw_want || GetTickCount() - g_ex_tick > 400 || !g_game_hwnd) { ig_hide(); return; }
    uintptr_t ctx = g_exit_obj; if (!ctx || !obj_alive3(ctx, g_exit_idx, g_exit_ser)) { ig_hide(); return; }
    RECT cr; POINT p0 = {0, 0}; GetClientRect(g_game_hwnd, &cr); ClientToScreen(g_game_hwnd, &p0);
    float box[4]; for (int k = 0; k < 4; k++) box[k] = g_ex_box[k];
    if (box[2] < p0.x - 50 || box[0] > p0.x + cr.right + 50) { box[0] += p0.x; box[2] += p0.x; box[1] += p0.y; box[3] += p0.y; }
    float x, y, W, H; sw_layout(box, p0.x, p0.y, cr.right, &x, &y, &W, &H);
    if (!sw_textures((void *)ctx, (int)W, (int)H)) { g_ig_fail++; logf_("theme: in-game switch: picture not made"); return; }
    uint8_t P[1024];
    if (!obj_alive3(IG.uw, IG.ui, IG.us) || !obj_alive3(IG.img, IG.ii, IG.is)) {
        memset(&IG, 0, sizeof IG); IG.dark = -1;
        memset(P, 0, sizeof P); fset(P, F_CREATE, "WorldContextObject", &ctx, 8); fset(P, F_CREATE, "WidgetType", &g_cls_uw, 8);
        fcall(g_wbl_cdo, F_CREATE, P);
        uintptr_t uw = 0; int ro = fp(F_CREATE, "ReturnValue"); if (ro >= 0) memcpy(&uw, P + ro, 8);
        int32_t wt_off = -1, rw_off = -1; uintptr_t tree = 0;
        prop_off(find_prop(g_cls_uw, "WidgetTree"), &wt_off); prop_off(find_prop(g_cls_wtree, "RootWidget"), &rw_off);
        if (!uw || wt_off < 0 || rw_off < 0 || !rdp(uw + wt_off, &tree) || !tree) { g_ig_fail++; logf_("theme: in-game switch: widget not created (%p)", (void *)uw); return; }
        memset(P, 0, sizeof P); fset(P, F_SPAWN, "ObjectClass", &g_cls_img, 8); fset(P, F_SPAWN, "Outer", &tree, 8);
        fcall(g_gs_cdo, F_SPAWN, P);
        uintptr_t img = 0; ro = fp(F_SPAWN, "ReturnValue"); if (ro >= 0) memcpy(&img, P + ro, 8);
        if (!img) { g_ig_fail++; logf_("theme: in-game switch: image not created"); return; }
        wr_(tree + rw_off, &img, 8);
        IG.uw = uw; IG.img = img; obj_id(uw, &IG.ui, &IG.us); obj_id(img, &IG.ii, &IG.is);
        memset(P, 0, sizeof P); uint8_t v = 1; fset(P, F_VIS, "InVisibility", &v, 1); fcall(uw, F_VIS, P);
        logf_("theme: in-game switch created");
    }
    memset(P, 0, sizeof P); fcall(IG.uw, F_INVP, P);
    { int ro = fp(F_INVP, "ReturnValue"); if (ro >= 0 && !P[ro]) { memset(P, 0, sizeof P); int32_t z = 100000; fset(P, F_ADDVP, "ZOrder", &z, 4); fcall(IG.uw, F_ADDVP, P); IG.x = IG.y = -99999; } }
    int dark = g_dark ? 1 : 0;
    if (IG.dark != dark || IG.tw != (int)W || IG.th != (int)H) {
        memset(P, 0, sizeof P); uintptr_t t = g_swt[dark]; uint8_t no = 0; fset(P, F_SETTEX, "Texture", &t, 8); fset(P, F_SETTEX, "bMatchSize", &no, 1);
        fcall(IG.img, F_SETTEX, P); IG.dark = dark; IG.tw = (int)W; IG.th = (int)H; IG.w = -1;
    }
    float sc = 1; { memset(P, 0, sizeof P); fset(P, F_VPSCALE, "WorldContextObject", &ctx, 8); fcall(g_wll_cdo, F_VPSCALE, P); int ro = fp(F_VPSCALE, "ReturnValue"); if (ro >= 0) memcpy(&sc, P + ro, 4); if (!(sc > 0.05f && sc < 20)) sc = 1; }
    float lx = x - p0.x, ly = y - p0.y;
    if (lx != IG.x || ly != IG.y) { memset(P, 0, sizeof P); float pos[2] = { lx, ly }; uint8_t yes = 1; fset(P, F_SETPOS, "Position", pos, 8); fset(P, F_SETPOS, "bRemoveDPIScale", &yes, 1); fcall(IG.uw, F_SETPOS, P); IG.x = lx; IG.y = ly; }
    if (W != IG.w || H != IG.h) { memset(P, 0, sizeof P); float sz[2] = { W / sc, H / sc }; fset(P, F_SETSIZE, "Size", sz, 8); fcall(IG.uw, F_SETSIZE, P); IG.w = W; IG.h = H; }
    float op = g_ex_op; if (op != IG.op) { memset(P, 0, sizeof P); fset(P, F_OPAC, "InOpacity", &op, 4); fcall(IG.uw, F_OPAC, P); IG.op = op; }
    if (!IG.vis) { memset(P, 0, sizeof P); uint8_t v = 3; fset(P, F_VIS, "InVisibility", &v, 1); fcall(IG.uw, F_VIS, P); IG.vis = 1; }  /* shown, clicks pass through */
    if (!g_ig_ok) { g_ig_ok = 1; logf_("theme: switch is drawn in the game (%.0fx%.0f, scale %.2f)", W, H, sc); }
}
static LRESULT CALLBACK game_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    g_hb_tick = GetTickCount();
    if (m == WM_CLOSE || m == WM_DESTROY || m == WM_QUIT || m == WM_ENDSESSION) g_shutdown = 1;     /* game closing: hands off */
    if (m == WM_THEME_NOW) { uintptr_t ctxo = g_exit_obj; if (ctxo && !obj_alive3(ctxo, g_exit_idx, g_exit_ser)) ctxo = 0; theme_pump((void *)ctxo); return 0; }      /* game thread: whole switch now */
    if (m == WM_THEME_TICK) { uintptr_t ctxo = g_exit_obj; if (ctxo && !obj_alive3(ctxo, g_exit_idx, g_exit_ser)) ctxo = 0; g_bc = 21; theme_pump((void *)ctxo); g_bc = 22; goaltab_tick((void *)ctxo); g_bc = 23; goalvid_tick(); g_bc = 0; g_tick_pending = 0; return 0; }
    if (m == WM_THEME_POS) { if (g_sw_cand) pos_query(); ig_update(); g_pos_pending = 0; return 0; }
    return CallWindowProcW(g_game_wp, h, m, w, l);
}
static DWORD WINAPI toggle_thread(LPVOID u) {
    (void)u;
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0}; wc.lpfnWndProc = tg_proc; wc.hInstance = hi; wc.lpszClassName = L"PSOThemeSwitch"; wc.hCursor = LoadCursor(NULL, IDC_HAND);
    RegisterClassW(&wc);
    g_tg_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, L"PSOThemeSwitch", L"", WS_POPUP, 0, 0, 64, 30, NULL, NULL, hi, NULL);
    int shown = 0, lastdark = -1;
    for (;;) {
        MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!g_game_hwnd || !IsWindow(g_game_hwnd)) { g_game_hwnd = NULL; EnumWindows(th_wnd_cb, (LPARAM)&g_game_hwnd); }
        static HWND owner;
        if (g_game_hwnd && owner != g_game_hwnd) {
            owner = g_game_hwnd; SetWindowLongPtrW(g_tg_wnd, GWLP_HWNDPARENT, (LONG_PTR)g_game_hwnd);
            WNDPROC prev = (WNDPROC)GetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC);
            if (prev && prev != game_wp) { g_game_wp = prev; if (SetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC, (LONG_PTR)game_wp)) { g_sub_hwnd = g_game_hwnd; logf_("theme: switch handler ready"); } }
        }
        RECT cr; POINT p0 = {0, 0};
        int ex = exit_poll();
        int ok = g_game_hwnd && g_in_menu && g_logged_in && ex && !g_overlay_menu && IsWindowVisible(g_game_hwnd) && !IsIconic(g_game_hwnd) && GetClientRect(g_game_hwnd, &cr) && ClientToScreen(g_game_hwnd, &p0);
        g_sw_cand = ok;
        { static DWORD lt; DWORD tn = GetTickCount();                   /* keep the colour updates running even when nothing moves in the menu */
          if (g_sub_hwnd && (!g_tick_pending || tn - lt > 1000) && tn - lt >= 100) { lt = tn; g_tick_pending = 1; PostMessageW(g_sub_hwnd, WM_THEME_TICK, 0, 0); } }
        if (g_sub_hwnd && !g_pos_pending && (ok || g_sw_want || IG.vis)) { g_pos_pending = 1; PostMessageW(g_sub_hwnd, WM_THEME_POS, 0, 0); }
        if (g_pos_pending && GetTickCount() - g_ex_tick > 500) g_pos_pending = 0;      /* message lost: ask again */
        static LONG seq0; static int was_ok;
        if (ok && !was_ok) seq0 = g_ex_seq;                              /* Exit just appeared: wait for fresh positions */
        was_ok = ok;
        if (ok && g_sub_hwnd && g_pos_ok != 0 && (g_ex_seq - seq0 < 2 || GetTickCount() - g_ex_tick >= 400)) ok = 0;   /* never the fixed spot while following works */
        g_sw_want = ok;
        if (ok) {
            int ch = cr.bottom - cr.top, cw = cr.right - cr.left;
            int H = ch * 36 / 1080, W, x, y; BYTE alpha = 255;
            int live = GetTickCount() - g_ex_tick < 400;
            if (live) {                                                  /* follow the Exit button as the menu animates */
                float bx[4]; for (int k = 0; k < 4; k++) bx[k] = g_ex_box[k];
                if (bx[2] < p0.x - 50 || bx[0] > p0.x + cw + 50) { bx[0] += p0.x; bx[2] += p0.x; bx[1] += p0.y; bx[3] += p0.y; }   /* window-relative */
                float fx, fy, fW, fH; sw_layout(bx, p0.x, p0.y, cw, &fx, &fy, &fW, &fH);
                x = (int)fx; y = (int)fy; H = (int)fH;
                float op = g_ex_op; alpha = (BYTE)(op <= 0 ? 0 : op >= 1 ? 255 : op * 255 + .5f);
            } else {
                x = p0.x + cw / 2 + ch * 814 / 1080; y = p0.y + ch * 45 / 1080 - H / 2;   /* right of Exit, same middle */
            }
            if (H < 12) H = 12;
            W = H * 2 + H / 6;
            if (x + W > p0.x + cw - 2) x = p0.x + cw - W - 2;
            int clip = y < p0.y ? p0.y - y : 0;                          /* still above the screen while sliding in */
            static int lastig = -1;
            if (W != g_tg_w || H != g_tg_h || lastdark != g_dark || lastig != g_ig_ok) { g_tg_w = W; g_tg_h = H; lastdark = g_dark; lastig = g_ig_ok; tg_render(g_tg_wnd, W, H); g_tg_x = -99999; }
            if (clip < H && alpha > 0) {
                if (x != g_tg_x || y != g_tg_y || clip != g_tg_clip || alpha != g_tg_alpha || !shown) {
                    g_tg_x = x; g_tg_y = y; g_tg_clip = clip; g_tg_alpha = alpha;
                    tg_present(g_tg_wnd, x, y, clip, alpha);
                }
                if (!shown) { ShowWindow(g_tg_wnd, SW_SHOWNOACTIVATE); shown = 1; }
            } else if (shown) { ShowWindow(g_tg_wnd, SW_HIDE); shown = 0; }
        } else if (shown) { ShowWindow(g_tg_wnd, SW_HIDE); shown = 0; }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 12, QS_ALLINPUT);
    }
}
static DWORD WINAPI theme_worker(LPVOID u) {
    (void)u;
    if (!refl_init()) return 0;
    for (int t = 0; t < 600; t++) {
        for_each_object(th_setup_cb, NULL);
        int ok = g_cls_world && g_cls_widget; for (unsigned i = 0; i < NSET; i++) if ((!g_set[i].cls || !g_set[i].fn) && (strcmp(SETTERS[i].cls, "RichTextBlock") || t < 10)) ok = 0;
        if (ok) break;
        Sleep(1000);
    }
    if (!g_cls_world || !g_cls_widget) { logf_("theme: game layout not found, off"); return 0; }
    setters_resolve();
    nh_install();
    prop_off(find_prop(g_cls_widget, "Visibility"), &o_w_vis); prop_off(find_prop(g_cls_widget, "Slot"), &o_w_slot); prop_off(find_prop(g_cls_widget, "RenderOpacity"), &o_w_opac);
    if (g_cls_panelslot) prop_off(find_prop(g_cls_panelslot, "Parent"), &o_ps_parent);
    if (g_cls_switcher) prop_off(find_prop(g_cls_switcher, "ActiveWidgetIndex"), &o_sw_active);
    if (g_cls_panelw) prop_off(find_prop(g_cls_panelw, "Slots"), &o_pw_slots);
    for (unsigned i = 0; i < NSET; i++) if (!strcmp(SETTERS[i].cls, "TextBlock") && g_set[i].cls) prop_off(find_prop(g_set[i].cls, "Text"), &o_tb_text);
    { char b[400] = ""; for (unsigned i = 0; i < NSET; i++) { char x[48]; snprintf(x, sizeof x, "%s.%s:%s%d ", SETTERS[i].cls, SETTERS[i].prop, g_set[i].fn ? "" : "nofn", g_set[i].off); strcat(b, x); } logf_("theme: ready (%s) %s", g_dark ? "moon" : "sun", b); }
    CloseHandle(CreateThread(NULL, 0, toggle_thread, NULL, 0, NULL));
    int last_dark = g_dark, logs = 0;
    for (;;) {
        ThScan s; memset(&s, 0, sizeof s);
        if (!g_cls_gi) { for_each_object(th_setup_cb, NULL); }
        if (g_cls_gi && o_gi_userid < 0) { if (!prop_off(find_prop(g_cls_gi, "UserId"), &o_gi_userid)) prop_off(find_prop(g_cls_gi, "UserID"), &o_gi_userid); }
        for_each_object(th_scan_cb, &s);
        int menu = s.worlds_menu > 0 && s.worlds_match == 0;
        if (menu != g_in_menu_world) { g_in_menu_world = menu; g_world_gen++; g_world_tick = GetTickCount(); }   /* level changed: per-world icons are rebuilt */
        if (s.logged != g_logged_in || menu != g_last_menu_log) { logf_("theme: main menu %d, logged in %d", menu, s.logged); g_last_menu_log = menu; }
        g_logged_in = s.logged; g_in_menu = menu;
        g_overlay_menu = s.overlay > 0;
        if (strcmp(s.vis, g_vis_last)) { static int vl; if (vl++ < 90) logf_("theme: visible menus%s: %s", menu ? "" : " (match)", s.vis); strcpy(g_vis_last, s.vis); }
        if ((s.exit_vis > 0) != g_exit_shown) { g_exit_shown = s.exit_vis > 0; logf_("theme: main menu Exit %s (%s)", g_exit_shown ? "visible" : "hidden", g_exit_root); }
        (void)last_dark;
        if (g_dark != g_pump_dark) { static DWORD since; if (!since) since = GetTickCount(); else if (GetTickCount() - since > 400) { EnterCriticalSection(&g_pcs); templates_set(g_dark); inst_set(g_dark); LeaveCriticalSection(&g_pcs); g_pump_dark = g_dark; since = 0; } }
        { static int n; if (++n % 10 == 0) { inst_cleanup(); rs_cleanup(); } }
        {   /* forget widgets that no longer exist */
            EnterCriticalSection(&g_thcs);
            int j = 0;
            for (int i = 0; i < g_nlive; i++) {
                LiveW *w = &g_live[i]; uintptr_t it = obj_item_rd(w->idx), o = 0; int32_t ser = 0;
                if (it && rdp(it, &o) && o == w->obj && rd(it + 0x10, &ser, 4) && ser == w->serial) g_live[j++] = *w; else free(w->orig);
            }
            g_nlive = j;
            LeaveCriticalSection(&g_thcs);
        }
        if ((s.new_tpl || logs < 3) && logs < 12) { logs++; logf_("theme: %d template colours, %ld live widgets", g_ntpl, g_nlive); }
        {   /* menus are rebuilt right after a level change: look often then, much less once things settle (and least in a match) */
            int settled = GetTickCount() - g_world_tick > 4000;
            Sleep(!settled ? 150 : s.worlds_match > 0 ? 2500 : 700);
        }
    }
}
static void theme_start(void) {
    InitializeCriticalSection(&g_thcs); InitializeCriticalSection(&g_pcs);
    wchar_t la[MAX_PATH]; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    swprintf(g_theme_file, MAX_PATH, L"%ls\\UE4PSO\\theme.txt", la);
    FILE *f = _wfopen(g_theme_file, L"r"); char b[16] = "";
    if (f) { fgets(b, sizeof b, f); fclose(f); }
    g_dark = !strncmp(b, "moon", 4);
    CloseHandle(CreateThread(NULL, 0, theme_worker, NULL, 0, NULL));
}

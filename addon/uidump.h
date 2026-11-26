/* uidump.h - F9 (test builds): writes the game's current menu widgets to %LOCALAPPDATA%\UE4PSO\ui-dump.txt,
 * so a new tab can be built as an exact copy of the game's own ones. Read-only. */
typedef struct { uintptr_t cls; int isw; char nm[48]; } UDc;
static UDc ud_c[4096];
static UDc *ud_cls(uintptr_t cls) {
    unsigned h = (unsigned)((cls >> 4) * 2654435761u) >> 20;
    for (int k = 0; k < 4096; k++, h = (h + 1) & 4095) {
        if (ud_c[h].cls == cls) return &ud_c[h];
        if (!ud_c[h].cls) { UDc *c = &ud_c[h]; c->cls = cls; c->nm[0] = 0; obj_name(cls, c->nm, sizeof c->nm);
            uintptr_t s = cls; char n[64];
            for (int d = 0; s && d < 24; d++) { if (obj_name(s, n, sizeof n) && !strcmp(n, "Widget")) { c->isw = 1; break; } uintptr_t up = 0; rdp(s + 0x40, &up); s = up; }
            return c; }
    }
    return NULL;
}
static FILE *ud_f; static int ud_n;
static int ud_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; UDc *c = ud_cls(cls); uint32_t fl = 0;
    if (!c || !c->isw || !rd(o + 8, &fl, 4) || (fl & 0x30) || ud_n > 30000) return 0;
    char nm[64] = "?", path[400] = ""; obj_name(o, nm, sizeof nm);
    uintptr_t ou = 0; rdp(o + 0x20, &ou);
    for (int d = 0; ou && d < 5; d++) { char on[64] = "?"; UDc *oc = ud_cls(obj_class(ou)); obj_name(ou, on, sizeof on);
        if (oc && (!strcmp(oc->nm, "WidgetBlueprintGeneratedClass") || !strcmp(oc->nm, "Package"))) { if (!strcmp(oc->nm, "WidgetBlueprintGeneratedClass")) return 0; break; }
        size_t L = strlen(path); snprintf(path + L, sizeof path - L, " < %s(%s)", on, oc ? oc->nm : "?");
        uintptr_t up = 0; rdp(ou + 0x20, &up); ou = up; }
    static int32_t o_slot = -2, o_par = -2, o_vis = -2; int32_t o_text = -1;
    if (o_slot == -2) { o_slot = -1; prop_off(find_prop(cls, "Slot"), &o_slot); }
    if (o_vis == -2) { o_vis = -1; prop_off(find_prop(cls, "Visibility"), &o_vis); }
    uintptr_t slot = 0, par = 0; char pn[64] = "-", sc[48] = "-", txt[64] = ""; unsigned char vis = 255;
    if (o_slot >= 0 && rdp(o + o_slot, &slot) && slot) { UDc *s = ud_cls(obj_class(slot)); if (s) snprintf(sc, sizeof sc, "%s", s->nm);
        if (o_par == -2) { o_par = -1; prop_off(find_prop(obj_class(slot), "Parent"), &o_par); }
        if (o_par >= 0 && rdp(slot + o_par, &par) && par) { obj_name(par, pn, sizeof pn); } }
    if (o_vis >= 0) rd(o + o_vis, &vis, 1);
    if ((!strcmp(c->nm, "TextBlock") || !strcmp(c->nm, "RichTextBlock")) && prop_off(find_prop(cls, "Text"), &o_text)) {
        uintptr_t td = 0; wchar_t w[64] = L""; if (rdp(o + o_text, &td) && text_of(td, w, 60)) { int z = 0; for (; w[z] && z < 62; z++) txt[z] = w[z] < 128 ? (char)w[z] : '?'; txt[z] = 0; } }
    fprintf(ud_f, "%s\t%s\tvis=%u\tparent=%s\tslot=%s\ttext=%s\tin%s\n", c->nm, nm, vis, pn, sc, txt, path); ud_n++;
    return 0;
}
static void uidump_run(void) {
    wchar_t la[MAX_PATH], p[MAX_PATH + 40]; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    swprintf(p, MAX_PATH + 40, L"%ls\\UE4PSO\\ui-dump.txt", la);
    ud_f = _wfopen(p, L"w"); if (!ud_f) return;
    ud_n = 0; for_each_object(ud_cb, NULL); fclose(ud_f); ud_f = NULL;
    logf_("ui dump: %d widgets written to ui-dump.txt", ud_n);
}

/* F9 in a match (test builds): writes how the local player's character is built (parts, materials, settings,
 * functions) to %LOCALAPPDATA%\UE4PSO\char-dump.txt - needed to change a character's real head. Read-only. */
static FILE *cd_f; static uintptr_t cd_pawn, cd_fcls, cd_cls[12]; static int cd_ncls;
static void cd_props(uintptr_t st, const char *ind) {
    uintptr_t p = 0; rdp(st + 0x50, &p);
    for (int n = 0; p && n < 600; n++) { uint32_t nm[2] = {0}, tn[2] = {0}; uintptr_t fc = 0, nx = 0; int32_t off = 0, sz = 0; char a[80] = "?", t[64] = "?";
        rd(p + 0x28, nm, 8); name_str(nm[0], nm[1], a, sizeof a); if (rdp(p + 8, &fc)) { rd(fc, tn, 8); name_str(tn[0], tn[1], t, sizeof t); }
        rd(p + 0x4C, &off, 4); rd(p + 0x3C, &sz, 4);
        char extra[80] = ""; if (!strcmp(t, "ObjectProperty") || !strcmp(t, "StructProperty") || !strcmp(t, "ByteProperty") || !strcmp(t, "EnumProperty")) { uintptr_t in = 0; if (rdp(p + 0x78, &in) && in) obj_name(in, extra, sizeof extra); }
        fprintf(cd_f, "%s%s\t%s %s\toff=%d size=%d\n", ind, a, t, extra, off, sz);
        if (!rdp(p + 0x20, &nx)) break; p = nx; }
}
static void cd_mat(uintptr_t m) {                                   /* a material's parent and its named settings (colours, numbers, pictures) */
    uintptr_t mc = obj_class(m), par = 0; int32_t po = -1; char n[96] = "-";
    if (prop_off(find_prop(mc, "Parent"), &po) && rdp(m + po, &par) && par) obj_name(par, n, sizeof n);
    fprintf(cd_f, "\t\tparent = %s\n", n);
    static const char *arrs[3] = { "VectorParameterValues", "ScalarParameterValues", "TextureParameterValues" };
    for (int k = 0; k < 3; k++) { uintptr_t ap = find_prop(mc, arrs[k]), inner = 0, st = 0; int32_t ao = -1, es = 0, vo = -1, io = -1; TArr a;
        if (!prop_off(ap, &ao) || !rdp(ap + 0x78, &inner) || !inner || !rd(inner + 0x3C, &es, 4) || !rdp(inner + 0x78, &st) || !st || es <= 0 || es > 256) continue;
        prop_off(find_prop(st, "ParameterInfo"), &io); prop_off(find_prop(st, "ParameterValue"), &vo);
        if (io < 0 || vo < 0 || !rd(m + ao, &a, sizeof a) || a.num <= 0 || a.num > 64) continue;
        for (int i = 0; i < a.num; i++) { uint32_t f2[2] = {0}; char pn[64] = "?"; rd(a.ptr + (uintptr_t)i * es + io, f2, 8); name_str(f2[0], f2[1], pn, sizeof pn);
            if (k == 0) { float v[4] = {0}; rd(a.ptr + (uintptr_t)i * es + vo, v, 16); fprintf(cd_f, "\t\tcolour '%s' = %.3f %.3f %.3f %.3f\n", pn, v[0], v[1], v[2], v[3]); }
            else if (k == 1) { float v = 0; rd(a.ptr + (uintptr_t)i * es + vo, &v, 4); fprintf(cd_f, "\t\tnumber '%s' = %.3f\n", pn, v); }
            else { uintptr_t tx = 0; char tn[96] = "-"; if (rdp(a.ptr + (uintptr_t)i * es + vo, &tx) && tx) obj_name(tx, tn, sizeof tn); fprintf(cd_f, "\t\tpicture '%s' = %s\n", pn, tn); } } }
}
static int cd_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; uintptr_t ou = 0; char n[96], c[64];
    if (!rdp(o + 0x20, &ou)) return 0;
    if (ou == cd_pawn) { obj_name(o, n, sizeof n); obj_name(cls, c, sizeof c); fprintf(cd_f, "PART\t%s\t%s\n", c, n);
        static const char *want[] = { "SkeletalMesh", "StaticMesh", "AttachParent", "AttachSocketName", "OverrideMaterials", "RelativeScale3D" };
        for (int i = 0; i < 6; i++) { int32_t off = -1; uintptr_t pr = find_prop(cls, want[i]); if (!prop_off(pr, &off)) continue;
            if (i < 3) { uintptr_t v = 0; char vn[96] = "-"; if (rdp(o + off, &v) && v) obj_name(v, vn, sizeof vn); fprintf(cd_f, "\t%s = %s\n", want[i], vn); }
            else if (i == 3) { uint32_t f2[2] = {0}; char vn[64] = "-"; rd(o + off, f2, 8); name_str(f2[0], f2[1], vn, sizeof vn); fprintf(cd_f, "\t%s = %s\n", want[i], vn); }
            else if (i == 4) { TArr a; if (rd(o + off, &a, sizeof a) && a.num > 0 && a.num < 32) for (int k = 0; k < a.num; k++) { uintptr_t m = 0, mc; char mn[96] = "-", mcn[64] = "-"; if (rdp(a.ptr + 8ULL * k, &m) && m) { obj_name(m, mn, sizeof mn); mc = obj_class(m); obj_name(mc, mcn, sizeof mcn); } fprintf(cd_f, "\tmaterial %d = %s (%s)\n", k, mn, mcn); if (m) cd_mat(m); } }
            else { float v[3] = {0}; rd(o + off, v, 12); fprintf(cd_f, "\tscale = %.2f %.2f %.2f\n", v[0], v[1], v[2]); } }
        return 0; }
    if (!cd_fcls && class_is(cls, "Function")) cd_fcls = cls;
    if (cls == cd_fcls) for (int i = 0; i < cd_ncls; i++) if (ou == cd_cls[i]) { obj_name(o, n, sizeof n); obj_name(ou, c, sizeof c); fprintf(cd_f, "FUNCTION\t%s\t%s\n", c, n); cd_props(o, "\t\tparam "); }
    return 0;
}
static int chardump_run(void) {
    wchar_t la[MAX_PATH], p[MAX_PATH + 40]; int32_t op = -1; uintptr_t pc = GW.pc, pawn = 0; char n[96];
    if (!pc || !prop_off(find_prop(obj_class(pc), "Pawn"), &op) || !rdp(pc + op, &pawn) || !pawn) return 0;
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return 0;
    swprintf(p, MAX_PATH + 40, L"%ls\\UE4PSO\\char-dump.txt", la); cd_f = _wfopen(p, L"w"); if (!cd_f) return 0;
    cd_pawn = pawn; cd_ncls = 0;
    for (uintptr_t c = obj_class(pawn); c && cd_ncls < 12; ) { obj_name(c, n, sizeof n); fprintf(cd_f, "CLASS\t%s\n", n); cd_cls[cd_ncls++] = c; int stop = !strcmp(n, "Character");
        if (!stop) cd_props(c, "\tvar "); uintptr_t up = 0; if (stop || !rdp(c + 0x40, &up)) break; c = up; }
    for_each_object(cd_cb, NULL); fclose(cd_f); cd_f = NULL;
    logf_("character list written to char-dump.txt");
    return 1;
}

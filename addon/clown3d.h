/* clown3d.h - Clown goal effect, the real 3D part: the keeper's own character is changed in the game.
 *   - hair: the biggest hair style the game has, cycling through rainbow colours (the game's own hair function)
 *   - face: the head's skin is swapped for a plain white material (face paint)
 *   - nose: a small red ball is attached to the head
 * Every player's game does this to its own copy of that character; nothing is sent to the server. When the
 * effect ends the nose is removed, the skin is put back and the game re-applies the player's own look.
 * Looking things up happens on the effects thread (reads only); every change is made on the game thread. */
typedef struct { const char *outer, *name; uintptr_t fn; } C3Fn;
enum { CF_ADDCOMP, CF_SETMESH, CF_MID, CF_SETMAT, CF_VEC, CF_SCA, CF_ATTACH, CF_WLOC, CF_WSCALE, CF_GETLOC, CF_FWD, CF_UP, CF_SOCKLOC, CF_COLL, CF_DESTROY, CF_MOBIL, CF_TEX, CF_N };
static C3Fn C3F[CF_N] = { {"Actor", "AddComponentByClass"}, {"StaticMeshComponent", "SetStaticMesh"}, {"PrimitiveComponent", "CreateDynamicMaterialInstance"}, {"PrimitiveComponent", "SetMaterial"},
    {"MaterialInstanceDynamic", "SetVectorParameterValue"}, {"MaterialInstanceDynamic", "SetScalarParameterValue"}, {"SceneComponent", "K2_AttachToComponent"}, {"SceneComponent", "K2_SetWorldLocation"},
    {"SceneComponent", "SetWorldScale3D"}, {"SceneComponent", "K2_GetComponentLocation"}, {"SceneComponent", "GetForwardVector"}, {"SceneComponent", "GetUpVector"}, {"SceneComponent", "GetSocketLocation"},
    {"PrimitiveComponent", "SetCollisionEnabled"}, {"ActorComponent", "K2_DestroyComponent"}, {"SceneComponent", "SetMobility"}, {"MaterialInstanceDynamic", "SetTextureParameterValue"} };
static struct {
    volatile LONG state;                                             /* 0 idle, 1 clown on, 2 put the real look back */
    uintptr_t ch, cls; int32_t idx, ser; DWORD t0, last; int step, logged, applied;
    uintptr_t fcls, ccls, mesh_cls, smc_cls;                         /* the game's Function / Class / StaticMesh classes, the StaticMeshComponent class */
    uintptr_t fn_hair, fn_apply, fn_owner, sphere, ballmesh, warm_ball; int warm_tries, listed;
    /* this keeper */
    uintptr_t mesh, cam, orig, skinpar, colpar, nosemesh, nose;
    int32_t oi, os, ni, ns, mi, ms;
    uint8_t n_color[4][8], n_tex[4][8], n_skin[8], n_sock[8]; uintptr_t texv[4]; int have_color, ntex, have_skin, have_sock; float skin; char colfrom[24]; float readback[4];
    int style, ostyle, have_ocol, nmat; float ocol[4], noseext;
    int face_on, hair_on; char note[200];
    uintptr_t haircomp; uint8_t n_hair[8]; int have_hair;                     /* the wig's colour is changed on its material directly (cheap), not by re-running the game's hair function */
    int variant, nsmid; uintptr_t smid[6]; int32_t si[6], ss[6]; float stone[6];      /* face way 3-6: the skin's own tone setting, on head, arms and legs */
} C3;
static uint8_t CP[1024];
static void c3_zero(void) { memset(CP, 0, sizeof CP); }
static int c3_set(int f, const char *p, const void *v, int n) { int o = gt_po(C3F[f].fn, p); if (o < 0 || o + n > 1000) return 0; memcpy(CP + o, v, n); return 1; }
static int c3_ret(int f, void *out, int n) { int o = gt_po(C3F[f].fn, "ReturnValue"); if (o < 0 || o + n > 1000) return 0; memcpy(out, CP + o, n); return 1; }
static void c3_call(uintptr_t obj, int f) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)obj, (void *)C3F[f].fn, CP); }
static void c3_callfn(uintptr_t obj, uintptr_t fn) { ch_pe_fn upe = (ch_pe_fn)(g_exe + RVA_PE); upe((void *)obj, (void *)fn, CP); }

/* ---- looking things up (effects thread) */
static int c3_cls_cb(uintptr_t o, uintptr_t cls, void *ctx) {        /* the engine's own classes: these never change while the game runs */
    (void)ctx; char n[64];
    if (!obj_name(o, n, sizeof n)) return 0;
    uintptr_t *slot = !strcmp(n, "Function") ? &C3.fcls : !strcmp(n, "Class") ? &C3.ccls : !strcmp(n, "StaticMesh") ? &C3.mesh_cls : !strcmp(n, "StaticMeshComponent") ? &C3.smc_cls : NULL;
    if (slot && !*slot && class_is(cls, "Class")) *slot = o;
    return C3.fcls && C3.ccls && C3.mesh_cls && C3.smc_cls;
}
static char c3_meshlist[700];
static int c3_scan_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; char n[64], on[64]; uintptr_t ou = 0;
    if (cls == C3.fcls) {
        if (!obj_name(o, n, sizeof n) || !rdp(o + 0x20, &ou) || !ou) return 0;
        if (!strcmp(n, "UpdateHairAppearance") || !strcmp(n, "ApplySP_Appearance")) { if (obj_name(ou, on, sizeof on) && !strcmp(on, "SP_Character_C")) { if (n[0] == 'U') C3.fn_hair = o; else C3.fn_apply = o; C3.fn_owner = ou; } return 0; }
        for (int i = 0; i < CF_N; i++) if (!C3F[i].fn && !strcmp(n, C3F[i].name) && obj_name(ou, on, sizeof on) && !strcmp(on, C3F[i].outer)) C3F[i].fn = o;
        return 0; }
    if (cls == C3.mesh_cls) { if (!obj_name(o, n, sizeof n)) return 0;
        if (!strcmp(n, "Sphere")) C3.sphere = o;
        if (!C3.listed) { size_t L = strlen(c3_meshlist); if (L < 640) snprintf(c3_meshlist + L, sizeof c3_meshlist - L, "%s ", n); }
        return 0; }
    if (C3.warm_ball && !C3.ballmesh && rdp(o + 0x20, &ou) && ou == C3.warm_ball) { int32_t so = -1; uintptr_t m = 0;      /* the ball's own round model */
        if (prop_off(find_prop(cls, "StaticMesh"), &so) && rdp(o + so, &m) && m && obj_class(m) == C3.mesh_cls) C3.ballmesh = m; }
    return 0;
}
static int c3_fns_ok(void) { for (int i = 0; i < CF_N; i++) if (!C3F[i].fn) return 0; return 1; }
static void clown3d_prewarm(void) {                                  /* in a match, before any goal: so nothing has to be searched when the goal comes */
    uintptr_t ball = GW.ball; if (!ball || C3.state) return;
    if (ball == C3.warm_ball && ((C3.fn_hair && C3.fn_apply && c3_fns_ok() && (C3.sphere || C3.ballmesh)) || C3.warm_tries >= 3)) return;
    if (ball != C3.warm_ball) { C3.warm_ball = ball; C3.warm_tries = 0; C3.fn_hair = C3.fn_apply = C3.fn_owner = 0; C3.ballmesh = C3.sphere = 0; }
    C3.warm_tries++;
    if (!C3.fcls || !C3.ccls || !C3.mesh_cls || !C3.smc_cls) for_each_object(c3_cls_cb, NULL);
    if (!C3.fcls) return;
    c3_meshlist[0] = 0; for_each_object(c3_scan_cb, NULL);
    if (!C3.listed) { C3.listed = 1; char miss[200] = ""; for (int i = 0; i < CF_N; i++) if (!C3F[i].fn) { size_t L = strlen(miss); if (L < 170) snprintf(miss + L, sizeof miss - L, "%s ", C3F[i].name); }
        logf_("clown 3D: hair functions %s, game functions %s%s, ball model %s, sphere model %s", C3.fn_hair && C3.fn_apply ? "found" : "missing", miss[0] ? "missing: " : "all found", miss, C3.ballmesh ? "found" : "missing", C3.sphere ? "found" : "missing");
        logf_("clown 3D: models loaded in the match: %s", c3_meshlist); }
}
/* entry i of a material's colour list (k 0) or number list (k 1): its name and value */
static int c3_mparam(uintptr_t m, int k, int i, uint8_t name[8], void *val, int nval) {
    static const char *arrs[3] = { "VectorParameterValues", "ScalarParameterValues", "TextureParameterValues" };
    uintptr_t mc = obj_class(m), ap = find_prop(mc, arrs[k]), inner = 0, st = 0; int32_t ao = -1, es = 0, vo = -1, io = -1; TArr a;
    if (!prop_off(ap, &ao) || !rdp(ap + 0x78, &inner) || !inner || !rd(inner + 0x3C, &es, 4) || !rdp(inner + 0x78, &st) || !st || es <= 0 || es > 256) return 0;
    prop_off(find_prop(st, "ParameterInfo"), &io); prop_off(find_prop(st, "ParameterValue"), &vo);
    if (io < 0 || vo < 0 || !rd(m + ao, &a, sizeof a) || i < 0 || i >= a.num || a.num > 64 || a.ptr < 0x10000) return 0;
    return rd(a.ptr + (uintptr_t)i * es + io, name, 8) && rd(a.ptr + (uintptr_t)i * es + vo, val, 4 * (SIZE_T)nval);      /* nval counts 4-byte units */
}
static int c3_mfind(uintptr_t m, int k, const char *want, uint8_t name[8], float *val, int nval) {      /* by name, else the first one */
    for (int i = 0; i < 16; i++) { uint8_t nm[8]; float v[4]; char s[64]; uint32_t f2[2];
        if (!c3_mparam(m, k, i, nm, v, nval)) break; memcpy(f2, nm, 8);
        if (name_str(f2[0], f2[1], s, sizeof s) && !strcmp(s, want)) { memcpy(name, nm, 8); memcpy(val, v, 4 * (size_t)nval); return 1; } }
    return c3_mparam(m, k, 0, name, val, nval);
}
static uintptr_t c3_comp(uintptr_t ch, const char *prop) { int32_t o = -1; uintptr_t c = 0; if (prop_off(find_prop(obj_class(ch), prop), &o)) rdp(ch + o, &c); return c; }
static uintptr_t c3_mat0(uintptr_t comp) { int32_t o = -1; TArr a; uintptr_t m = 0; if (comp && prop_off(find_prop(obj_class(comp), "OverrideMaterials"), &o) && rd(comp + o, &a, sizeof a) && a.num > 0 && a.num < 32) rdp(a.ptr, &m); return m; }
static uintptr_t c3_parent(uintptr_t m) { int32_t o = -1; uintptr_t p = 0; if (m && prop_off(find_prop(obj_class(m), "Parent"), &o)) rdp(m + o, &p); return p; }
/* the game's list of hair styles (number -> model): the bulkiest one is the clown wig; also which number the keeper wears now */
static void c3_hair_pick(uintptr_t ch, uintptr_t cur_mesh) {
    uintptr_t mp = find_prop(obj_class(ch), "SP_HairStyle"), kp = 0, vp = 0; int32_t mo = -1, voff = 8, esz = 24, ksz = 4, lay[6]; TArr a; char tn[48] = "", line[300] = "";
    C3.style = -1; C3.ostyle = -1;
    if (!prop_off(mp, &mo) || !rd(ch + mo, &a, sizeof a) || a.num <= 0 || a.num > 200 || a.ptr < 0x10000) return;
    if (rdp(mp + 0x80, &vp) && vp && fld_type(vp, tn, sizeof tn) && strcmp(tn, "ObjectProperty")) { snprintf(C3.note, sizeof C3.note, "hair list holds %s", tn); return; }
    if (rdp(mp + 0x78, &kp) && kp) rd(kp + 0x3C, &ksz, 4);
    if (rd(mp + 0x88, lay, 24) && lay[0] >= 4 && lay[0] <= 16 && lay[5] >= lay[0] + 8 && lay[5] <= 64) { voff = lay[0]; esz = lay[5]; }
    float bestm = 0;
    for (int i = 0; i < a.num; i++) { uintptr_t e = a.ptr + (uintptr_t)i * esz, m = 0; int32_t key = 0; char n[64]; float b[7]; int32_t bo = -1;
        if (!rd(e, &key, 4) || !rd(e + voff, &m, 8)) continue;
        if (ksz == 1) key &= 255;
        if (!m) { if (!cur_mesh && C3.ostyle < 0 && key >= 0 && key <= 500) C3.ostyle = key; continue; }      /* the 'no hair' entry */
        if (key < 0 || key > 500 || !obj_name(m, n, sizeof n) || !class_is(obj_class(m), "SkeletalMesh")) continue;
        if (m == cur_mesh) C3.ostyle = key;
        if (!(prop_off(find_prop(obj_class(m), "ImportedBounds"), &bo) || prop_off(find_prop(obj_class(m), "ExtendedBounds"), &bo)) || !rd(m + bo, b, 28)) continue;
        float mn = b[3] < b[4] ? b[3] : b[4]; if (b[5] < mn) mn = b[5];
        if (!(mn > 0 && mn < 500)) continue;
        { size_t L = strlen(line); if (L < 260) snprintf(line + L, sizeof line - L, "%d=%s %.0fx%.0fx%.0f, ", key, n, b[3], b[4], b[5]); }
        if (mn > bestm) { bestm = mn; C3.style = key; } }
    if (C3.logged < 2) logf_("clown 3D: hair styles: %s-> wig %d, the keeper wears %d", line, C3.style, C3.ostyle);
}
static void clown3d_begin(void) {                                    /* effects thread */
    uintptr_t pawn = gs_victim_pawn, cls, c, hair; if (!pawn) return;
    for (int w = 0; w < 80 && C3.state; w++) Sleep(5);                /* a replay right after another: let the game put the last one's look back first */
    if (C3.state) return;
    cls = obj_class(pawn); if (!gs_is_char(cls)) return;
    if (GW.ball && (GW.ball != C3.warm_ball || !C3.fn_hair)) { C3.warm_tries = 0; clown3d_prewarm(); }
    int own = 0; c = cls; for (int d = 0; c && d < 6 && !own; d++) { if (c == C3.fn_owner) own = 1; else if (!rdp(c + 0x40, &c)) break; }
    uintptr_t o1 = 0, o2 = 0;
    if (!own || !C3.fn_hair || !C3.fn_apply || obj_class(C3.fn_hair) != C3.fcls || obj_class(C3.fn_apply) != C3.fcls || !rdp(C3.fn_hair + 0x20, &o1) || !rdp(C3.fn_apply + 0x20, &o2) || o1 != C3.fn_owner || o2 != C3.fn_owner) {
        C3.warm_ball = 0; if (C3.logged++ < 3) logf_("clown 3D: the game's hair functions weren't found, using the drawn head"); return; }
    C3.note[0] = 0; C3.face_on = C3.hair_on = C3.applied = 0; C3.nose = 0; C3.nmat = 1; C3.noseext = 0;
    C3.mesh = c3_comp(pawn, "Mesh"); C3.cam = c3_comp(pawn, "1stPersonCamera"); hair = c3_comp(pawn, "Hair");
    /* the head's skin as it is now, and what it is made from (to put it back) */
    C3.orig = c3_mat0(C3.mesh); C3.skinpar = c3_parent(C3.orig); C3.oi = -1; C3.os = 0; if (C3.orig) obj_id(C3.orig, &C3.oi, &C3.os);
    C3.have_skin = C3.orig && c3_mfind(C3.orig, 1, "SkinTone", C3.n_skin, &C3.skin, 1);
    /* a plain cloth material the game already has, whose colours can all be set: the socks' (else shorts', gloves', wristbands').
     * Every colour it has is set to the wanted one and its pictures are kept, so it looks exactly like plain one-colour socks. */
    C3.variant = g_clown_face; if (C3.variant < 1 || C3.variant > CLOWN_FACES) C3.variant = 1;
    { static const char *pa[4] = { "Socks", "Shorts", "Gloves", "Wristbands" }, *pb[4] = { "Shorts", "Socks", "Gloves", "Wristbands" }; const char **parts = C3.variant == 2 ? pb : pa;
      C3.colpar = 0; C3.have_color = 0; C3.ntex = 0; C3.colfrom[0] = 0;
      for (int p = 0; p < 4 && !C3.have_color; p++) { uintptr_t gm = c3_mat0(c3_comp(pawn, parts[p])), par = c3_parent(gm); float v[4]; if (!gm || !par) continue;
          int nc = 0; while (nc < 4 && c3_mparam(gm, 0, nc, C3.n_color[nc], v, 4)) nc++;
          if (!nc) continue;
          C3.colpar = par; C3.have_color = nc; snprintf(C3.colfrom, sizeof C3.colfrom, "%s", parts[p]);
          while (C3.ntex < 4 && c3_mparam(gm, 2, C3.ntex, C3.n_tex[C3.ntex], &C3.texv[C3.ntex], 2)) C3.ntex++; } }
    /* every skin material on the character (head, arms on the shirt, legs on the shorts) and its tone now */
    { static const char *sk[3] = { "Mesh", "Shirt", "Shorts" }; C3.nsmid = 0;
      for (int p = 0; p < 3 && C3.skinpar; p++) { uintptr_t comp = c3_comp(pawn, sk[p]); int32_t o = -1; TArr a;
          if (!comp || !prop_off(find_prop(obj_class(comp), "OverrideMaterials"), &o) || !rd(comp + o, &a, sizeof a) || a.num <= 0 || a.num > 8) continue;
          for (int k = 0; k < a.num && C3.nsmid < 6; k++) { uintptr_t m = 0; uint8_t nm[8]; float v = 0;
              if (!rdp(a.ptr + 8ULL * k, &m) || !m || c3_parent(m) != C3.skinpar || !c3_mfind(m, 1, "SkinTone", nm, &v, 1)) continue;
              C3.smid[C3.nsmid] = m; obj_id(m, &C3.si[C3.nsmid], &C3.ss[C3.nsmid]); C3.stone[C3.nsmid] = v; C3.nsmid++; } } }
    /* where the head is: the same point the first-person camera hangs on */
    { int32_t so = -1; uint32_t f2[2] = {0}; C3.have_sock = C3.cam && prop_off(find_prop(obj_class(C3.cam), "AttachSocketName"), &so) && rd(C3.cam + so, C3.n_sock, 8); memcpy(f2, C3.n_sock, 8); if (!f2[0]) C3.have_sock = 0; }
    /* hair: the wig, and the keeper's own style + colour */
    { uintptr_t hm = 0, hmat = c3_mat0(hair); int32_t so = -1;
      if (hair && prop_off(find_prop(obj_class(hair), "SkeletalMesh"), &so)) rdp(hair + so, &hm);
      c3_hair_pick(pawn, hm);
      C3.have_ocol = hmat && c3_mfind(hmat, 0, "Color", C3.n_hair, C3.ocol, 4); C3.have_hair = C3.have_ocol; C3.haircomp = hair; }
    /* the nose: a perfect sphere if the game has one loaded, otherwise the ball's model */
    C3.nosemesh = C3.sphere ? C3.sphere : C3.ballmesh; C3.mi = -1; C3.ms = 0;
    if (C3.nosemesh) { int32_t bo = -1, mo = -1; float b[7]; TArr a; obj_id(C3.nosemesh, &C3.mi, &C3.ms);
        if (prop_off(find_prop(obj_class(C3.nosemesh), "ExtendedBounds"), &bo) && rd(C3.nosemesh + bo, b, 28) && b[3] > 1 && b[3] < 500) C3.noseext = (b[3] + b[4] + b[5]) / 3;
        if (prop_off(find_prop(obj_class(C3.nosemesh), "StaticMaterials"), &mo) && rd(C3.nosemesh + mo, &a, sizeof a) && a.num >= 1 && a.num <= 8) C3.nmat = a.num; }
    if (C3.logged < 2) { char hn[64] = "-"; int32_t so = -1, bo = -1; uintptr_t hm = 0; float b[7] = {0};
        if (C3.mesh && prop_off(find_prop(obj_class(C3.mesh), "SkeletalMesh"), &so) && rdp(C3.mesh + so, &hm) && hm) { obj_name(hm, hn, sizeof hn); if (prop_off(find_prop(obj_class(hm), "ImportedBounds"), &bo)) rd(hm + bo, b, 28); }
        logf_("clown 3D: head model %s (middle %.0f %.0f %.0f, half size %.0f %.0f %.0f), skin %s, colour material %s, head point %s, nose model %s (size %.1f, %d part(s))", hn, b[0], b[1], b[2], b[3], b[4], b[5],
              C3.have_skin ? "ok" : "not read", C3.have_color ? C3.colfrom : "missing", C3.have_sock ? "ok" : "missing", C3.nosemesh ? (C3.sphere ? "sphere" : "ball") : "missing", C3.noseext, C3.nmat); }
    C3.ch = pawn; C3.cls = cls; obj_id(pawn, &C3.idx, &C3.ser); C3.t0 = GetTickCount(); C3.last = 0; C3.step = 0; C3.state = 1;
    g_fx_3d = 1;                                                     /* the drawn head isn't shown, not even for the first frames */
}
static void clown3d_end(void) { InterlockedCompareExchange(&C3.state, 2, 1); }   /* effects thread: put the real look back at the next chance */

/* ---- changing the character (game thread only) */
static uintptr_t c3_colour_mat(uintptr_t comp, int elem, const float col[4]) {    /* give one part of a model the plain material in this colour */
    if (!C3F[CF_MID].fn || !C3F[CF_VEC].fn || !C3.colpar || !C3.have_color) return 0;
    c3_zero(); int32_t e = elem; uintptr_t mid = 0;
    if (!c3_set(CF_MID, "ElementIndex", &e, 4) || !c3_set(CF_MID, "SourceMaterial", &C3.colpar, 8)) return 0;
    c3_call(comp, CF_MID); c3_ret(CF_MID, &mid, 8); if (!mid) return 0;
    for (int i = 0; i < C3.ntex && C3F[CF_TEX].fn; i++) { if (!C3.texv[i]) continue; c3_zero(); if (c3_set(CF_TEX, "ParameterName", C3.n_tex[i], 8) && c3_set(CF_TEX, "Value", &C3.texv[i], 8)) c3_call(mid, CF_TEX); }
    for (int i = 0; i < C3.have_color; i++) { c3_zero(); if (c3_set(CF_VEC, "ParameterName", C3.n_color[i], 8) && c3_set(CF_VEC, "Value", col, 16)) c3_call(mid, CF_VEC); }
    { uint8_t nm[8]; memset(C3.readback, 0, 16); c3_mparam(mid, 0, 0, nm, C3.readback, 4); }      /* what the game now holds for it (for the log) */
    return mid;
}
static int c3_getvec(uintptr_t comp, int f, float out[3]) { c3_zero(); c3_call(comp, f); return c3_ret(f, out, 12) && gw_ok3(out); }
static void c3_nose(void) {
    static const float red[4] = { 0.92f, 0.02f, 0.03f, 1 };
    if (!c3_fns_ok() || !C3.smc_cls || !C3.nosemesh || !C3.mesh || !C3.cam || !C3.have_sock || !C3.have_color || C3.noseext <= 0) { snprintf(C3.note, sizeof C3.note, "nose skipped (something it needs is missing)"); return; }
    if (!obj_alive3(C3.nosemesh, C3.mi, C3.ms) || obj_class(C3.nosemesh) != C3.mesh_cls) { snprintf(C3.note, sizeof C3.note, "nose skipped (its model is gone)"); return; }
    float Cc[3], F[3], U[3], S[3];
    c3_zero(); if (!c3_set(CF_SOCKLOC, "InSocketName", C3.n_sock, 8)) return; c3_call(C3.mesh, CF_SOCKLOC); if (!c3_ret(CF_SOCKLOC, S, 12) || !gw_ok3(S)) return;
    if (!c3_getvec(C3.cam, CF_GETLOC, Cc) || !c3_getvec(C3.cam, CF_FWD, F) || !c3_getvec(C3.cam, CF_UP, U)) return;
    float d = (Cc[0] - S[0]) * F[0] + (Cc[1] - S[1]) * F[1] + (Cc[2] - S[2]) * F[2], h = (Cc[0] - S[0]) * U[0] + (Cc[1] - S[1]) * U[1] + (Cc[2] - S[2]) * U[2];
    float fwd = d - 2, up = (h >= 3 && h <= 16) ? h - 4.5f : 4.5f; if (fwd < 9) fwd = 9; if (fwd > 13) fwd = 13;
    float pos[3] = { S[0] + F[0] * fwd + U[0] * up, S[1] + F[1] * fwd + U[1] * up, S[2] + F[2] * fwd + U[2] * up }, k = 4.3f / C3.noseext, sc[3] = { k, k, k };
    /* the new part: created by the game on that character, no collisions, then shaped, coloured, placed and fixed to the head */
    float tr[12] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0 }; uint8_t one = 1, mov = 2, none = 0, keepworld = 1; uintptr_t comp = 0;
    c3_zero(); if (!c3_set(CF_ADDCOMP, "Class", &C3.smc_cls, 8) || !c3_set(CF_ADDCOMP, "bManualAttachment", &one, 1) || !c3_set(CF_ADDCOMP, "RelativeTransform", tr, 48)) return;
    c3_call(C3.ch, CF_ADDCOMP); c3_ret(CF_ADDCOMP, &comp, 8);
    if (!comp || obj_class(comp) != C3.smc_cls) { snprintf(C3.note, sizeof C3.note, "nose skipped (the game didn't create the part)"); return; }
    C3.nose = comp; obj_id(comp, &C3.ni, &C3.ns);
    c3_zero(); if (c3_set(CF_COLL, "NewType", &none, 1)) c3_call(comp, CF_COLL);
    c3_zero(); if (c3_set(CF_MOBIL, "NewMobility", &mov, 1)) c3_call(comp, CF_MOBIL);
    c3_zero(); if (c3_set(CF_SETMESH, "NewMesh", &C3.nosemesh, 8)) c3_call(comp, CF_SETMESH);
    for (int i = 0; i < C3.nmat; i++) c3_colour_mat(comp, i, red);
    c3_zero(); if (c3_set(CF_WLOC, "NewLocation", pos, 12) && c3_set(CF_WLOC, "bTeleport", &one, 1)) c3_call(comp, CF_WLOC);
    c3_zero(); if (c3_set(CF_WSCALE, "NewScale", sc, 12)) c3_call(comp, CF_WSCALE);
    c3_zero(); if (c3_set(CF_ATTACH, "Parent", &C3.mesh, 8) && c3_set(CF_ATTACH, "SocketName", C3.n_sock, 8) && c3_set(CF_ATTACH, "LocationRule", &keepworld, 1) && c3_set(CF_ATTACH, "RotationRule", &keepworld, 1) && c3_set(CF_ATTACH, "ScaleRule", &keepworld, 1)) c3_call(comp, CF_ATTACH);
    snprintf(C3.note, sizeof C3.note, "nose on (camera %.1f ahead of / %.1f above the head point, nose put %.1f ahead / %.1f above)", d, h, fwd, up);
}
static void c3_hair(int style, const float col[4]) {
    struct { int32_t style; float col[4]; } p; p.style = style; memcpy(p.col, col, 16);
    c3_zero(); memcpy(CP, &p, sizeof p); c3_callfn(C3.ch, C3.fn_hair);
}
static void c3_on(void) {                                            /* once, when the effect starts */
    static const float white[4] = { 0.95f, 0.95f, 0.95f, 1 };
    C3.applied = 1;
    static const float tones[CLOWN_FACES + 1] = { 0, 0, 0, 0, -1, 1, 2 }; float fc[4] = {0};
    if (C3.variant <= 2) { if (C3.mesh && C3.orig && C3.skinpar && c3_colour_mat(C3.mesh, 0, white)) { C3.face_on = 1; memcpy(fc, C3.readback, 16); } }
    else if (C3.have_skin && C3F[CF_SCA].fn) { float v = tones[C3.variant];      /* the skin's own tone setting pushed to its light end (or past it) */
        for (int i = 0; i < C3.nsmid; i++) if (obj_alive3(C3.smid[i], C3.si[i], C3.ss[i]) && c3_parent(C3.smid[i]) == C3.skinpar) { c3_zero(); if (c3_set(CF_SCA, "ParameterName", C3.n_skin, 8) && c3_set(CF_SCA, "Value", &v, 4)) { c3_call(C3.smid[i], CF_SCA); C3.face_on = 2; } } }
    c3_nose();
    if (C3.logged < 30) { C3.logged++; logf_("clown 3D: on the keeper - wig style %d, face way %d %s (cloth material from %s, %d colour(s), %d picture(s), reads %.2f %.2f %.2f; %d skin part(s), tone was %.2f), %s", C3.style, C3.variant, C3.face_on ? "on" : "not possible here", C3.colfrom, C3.have_color, C3.ntex, fc[0], fc[1], fc[2], C3.nsmid, C3.nsmid ? C3.stone[0] : -1.f, C3.note[0] ? C3.note : "nose: not possible here"); }
}
static void c3_off(void) {                                           /* the real look back */
    if (!C3.applied) return;
    if (C3.nose && obj_alive3(C3.nose, C3.ni, C3.ns) && obj_class(C3.nose) == C3.smc_cls && C3F[CF_DESTROY].fn) { c3_zero(); if (c3_set(CF_DESTROY, "Object", &C3.nose, 8)) c3_call(C3.nose, CF_DESTROY); }
    C3.nose = 0;
    if (C3.face_on == 2) { C3.face_on = 0;                              /* tone back to what it was */
        for (int i = 0; i < C3.nsmid; i++) if (obj_alive3(C3.smid[i], C3.si[i], C3.ss[i]) && c3_parent(C3.smid[i]) == C3.skinpar) { c3_zero(); if (c3_set(CF_SCA, "ParameterName", C3.n_skin, 8) && c3_set(CF_SCA, "Value", &C3.stone[i], 4)) c3_call(C3.smid[i], CF_SCA); } }
    if (C3.face_on) { int32_t e = 0; uintptr_t mid = 0; C3.face_on = 0;
        if (obj_alive3(C3.orig, C3.oi, C3.os) && c3_parent(C3.orig) == C3.skinpar && C3F[CF_SETMAT].fn) { c3_zero(); if (c3_set(CF_SETMAT, "ElementIndex", &e, 4) && c3_set(CF_SETMAT, "Material", &C3.orig, 8)) c3_call(C3.mesh, CF_SETMAT); }
        else { c3_zero(); if (c3_set(CF_MID, "ElementIndex", &e, 4) && c3_set(CF_MID, "SourceMaterial", &C3.skinpar, 8)) { c3_call(C3.mesh, CF_MID); c3_ret(CF_MID, &mid, 8); }      /* the old skin was cleaned up meanwhile: a new one, same tone */
            if (mid && C3.have_skin && C3F[CF_SCA].fn) { c3_zero(); if (c3_set(CF_SCA, "ParameterName", C3.n_skin, 8) && c3_set(CF_SCA, "Value", &C3.skin, 4)) c3_call(mid, CF_SCA); } } }
    if (C3.hair_on && C3.ostyle >= 0 && C3.have_ocol) c3_hair(C3.ostyle, C3.ocol);
    C3.hair_on = 0;
    c3_zero(); c3_callfn(C3.ch, C3.fn_apply);                        /* the game re-applies that player's own look */
}
static void clown3d_tick(void) {                                     /* game thread */
    static const float rb[6][4] = { {1,0.05f,0.05f,1}, {1,0.45f,0,1}, {1,0.9f,0,1}, {0.05f,0.8f,0.1f,1}, {0.05f,0.3f,1,1}, {0.6f,0.1f,0.9f,1} };
    static int busy; LONG st = C3.state; if (!st || busy) return;
    DWORD now = GetTickCount(); if (st == 1 && C3.step && now - C3.last < 200) return; C3.last = now;
    busy = 1;
    if (!obj_alive3(C3.ch, C3.idx, C3.ser) || obj_class(C3.ch) != C3.cls) { C3.state = 0; C3.nose = 0; g_fx_3d = 0; busy = 0; return; }      /* that player left */
    if (st == 2 || now - C3.t0 > 4600) { c3_off(); C3.state = 0; g_fx_3d = 0; busy = 0; return; }
    if (!C3.step) c3_on();
    int st2 = C3.style >= 0 ? C3.style : C3.ostyle;                       /* no wig found: the keeper's own hair, in clown colours */
    if (st2 >= 0) { uintptr_t hmid = C3.step && C3.have_hair && C3.haircomp && C3F[CF_VEC].fn ? c3_mat0(C3.haircomp) : 0;
        if (hmid && class_is(obj_class(hmid), "MaterialInstanceDynamic")) { c3_zero(); if (c3_set(CF_VEC, "ParameterName", C3.n_hair, 8) && c3_set(CF_VEC, "Value", rb[C3.step % 6], 16)) c3_call(hmid, CF_VEC); }      /* later colour steps: just the colour */
        else c3_hair(st2, rb[C3.step % 6]);                                /* first step (puts the wig on), or no direct way */
        C3.hair_on = 1; }
    C3.step++; g_fx_3d = C3.hair_on || C3.face_on; busy = 0;
}

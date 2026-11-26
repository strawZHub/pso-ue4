/* goalsync.h - everyone sees the SCORER's goal effect.
 * The server keeps each player's chosen effect (like name-tag colours). Every add-on downloads that list.
 * When a goal is scored the add-on works out who scored (by the name in the goal message, or by whose
 * goal count went up) and plays that player's effect. Players who never chose one get the Shockwave. */
#define GS_MAX 600
typedef struct { uintptr_t ps; int goals; char sid[20]; wchar_t name[40]; } GsPl;
static GsPl gs_snap[48]; static int gs_nsnap; static uintptr_t gs_gs_cls, gs_gs; static int gs_miss; static DWORD gs_miss_t;
static char gs_last_scorer[20];
static uintptr_t gs_victim_pawn;
#define GS_DEFAULT_FX 1
typedef struct { char sid[20]; int fx; } GsEnt;
static GsEnt g_gsl[2][GS_MAX]; static int g_gsn[2]; static volatile int g_gsi, g_gs_have;
static volatile LONG g_gs_now;
static int gs_is_sid(const char *s) { if (strncmp(s, "7656119", 7) || strlen(s) != 17) return 0; for (int i = 0; i < 17; i++) if (s[i] < '0' || s[i] > '9') return 0; return 1; }
static void gs_my_sid(char *out, int cap) {                          /* my own SteamID, from the game */
    static char cache[20]; out[0] = 0;
    if (cache[0]) { snprintf(out, cap, "%s", cache); return; }
    uintptr_t gi = g_gi_obj; char id[64]; int32_t o = -1;
    static const char *names[] = { "PlayerId", "UserId", "SteamID", "SteamId", "PlayerID" };
    if (!gi) return;
    for (int i = 0; i < 5; i++) if (prop_off(find_prop(obj_class(gi), names[i]), &o) && get_fstring(gi, o, id, sizeof id) && gs_is_sid(id)) { snprintf(cache, sizeof cache, "%s", id); snprintf(out, cap, "%s", id); return; }
}
static int gs_http(const wchar_t *verb, const wchar_t *path, const char *body, char *out, int cap) {     /* returns the HTTP status (0 = no answer) */
    int code = 0; wchar_t host[128]; if (!g_tsrv[0]) return 0; MultiByteToWideChar(CP_UTF8, 0, g_tsrv, -1, host, 128);
    HINTERNET s = WinHttpOpen(L"pso", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0), c = NULL, r = NULL; if (!s) return 0;
    WinHttpSetTimeouts(s, 4000, 4000, 4000, 6000); if (out && cap) out[0] = 0;
    if ((c = WinHttpConnect(s, host, 443, 0)) && (r = WinHttpOpenRequest(c, verb, path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE))) {
        DWORD fl = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID, n = body ? (DWORD)strlen(body) : 0;
        WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &fl, sizeof fl);
        if (WinHttpSendRequest(r, body ? L"Content-Type: application/json\r\n" : NULL, body ? (DWORD)-1 : 0, (LPVOID)body, n, n, 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD st = 0, sz = sizeof st, len = 0, got = 0; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &st, &sz, NULL); code = (int)st;
            while (out && len < (DWORD)cap - 1 && WinHttpReadData(r, out + len, cap - 1 - len, &got) && got) len += got;
            if (out && cap) out[len] = 0;
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return code;
}
static int gs_lookup(const char *sid) { int t = g_gsi; for (int i = 0; i < g_gsn[t]; i++) if (!strcmp(g_gsl[t][i].sid, sid)) return g_gsl[t][i].fx; return 0; }
static void gs_fetch(void) {
    static char buf[GS_MAX * 24 + 64]; int code = gs_http(L"GET", L"/pso-goalfx", NULL, buf, sizeof buf);
    if (code != 200 || strncmp(buf, "#goalfx", 7)) { static int lg; if (lg++ < 3) logf_("goal effects: the server has no list of players' effects yet (answer %d) - update the server program", code); return; }
    int w = !g_gsi, n = 0;
    for (char *line = buf, *nx; line && *line && n < GS_MAX; line = nx) { nx = strchr(line, '\n'); if (nx) *nx++ = 0; line[strcspn(line, "\r")] = 0;
        char *tab = strchr(line, '\t'); if (!tab) continue; *tab++ = 0; int fx = atoi(tab);
        if (gs_is_sid(line) && fx >= 1 && fx <= GFX_N) { snprintf(g_gsl[w][n].sid, 20, "%s", line); g_gsl[w][n].fx = fx; n++; } }
    g_gsn[w] = n; g_gsi = w;
    { static int last = -1; if (n != last) { last = n; logf_("goal effects: %d player(s) have chosen an effect", n); } }
    char me[20]; gs_my_sid(me, sizeof me);
    if (me[0] && !g_gs_post && !g_gs_unsynced) { int fx = gs_lookup(me); if (fx && !g_gs_have) { GT.saved = fx; g_gfx_sel = fx; } }      /* my choice as the server knows it (new PC / reinstall) - unless a newer choice made here is still on its way */
    g_gs_have = 1;
}
static DWORD WINAPI gs_thread(LPVOID u) {
    (void)u; DWORD last = 0; int was_menu = 1;
    for (;;) {
        Sleep(400);
        {   /* my choice -> the server. If it doesn't get through (server busy, restarted, the game not logged in yet) it is
             * tried again: every 5 s at first, then every 30 s, until the server says ok. */
            static LONG pend; static DWORD pend_t; static int pend_tries; DWORD tn = GetTickCount();
            LONG p = InterlockedExchange(&g_gs_post, 0); if (p) { pend = p; pend_tries = 0; pend_t = 0; }
            if (pend && pend_tries < 60 && (!pend_t || tn - pend_t > (DWORD)(pend_tries < 4 ? 5000 : 30000))) {
                char me[20], body[96], ans[80] = ""; int code = 0, ok = 0; gs_my_sid(me, sizeof me); pend_t = tn ? tn : 1;
                if (me[0]) { snprintf(body, sizeof body, "{\"steamid\":\"%s\",\"fx\":%ld}", me, pend); code = gs_http(L"POST", L"/pso-goalfx", body, ans, sizeof ans); ok = code == 200 && !strncmp(ans, "ok", 2); }
                if (ok) { logf_("goal effects: choice %ld saved on the server - other players will see it%s", pend, pend_tries ? " (after trying again)" : ""); gt_file((int)pend, 0); g_gs_unsynced = 0; pend = 0; last = 0; }
                else { ans[60] = 0; for (char *q = ans; *q; q++) if (*q < 32) *q = ' ';
                    if (pend_tries == 0 || pend_tries == 4 || pend_tries % 20 == 19) logf_("goal effects: choice %ld not saved on the server yet (%s%s%s), trying again", pend, !me[0] ? "my SteamID isn't known yet" : code == 0 ? "no answer" : code == 403 ? "refused: " : code == 200 ? "the server program is the old one" : "answer: ", me[0] && code && code != 200 ? ans : "", "");
                    pend_tries++; } }
        }
        DWORD now = GetTickCount(); int menu = g_in_menu;
        if (!last || now - last > 60000 || (was_menu && !menu) || InterlockedExchange(&g_gs_now, 0)) { last = now ? now : 1; gs_fetch(); }   /* at start, every minute, and when a match begins */
        if (was_menu && !menu) gs_gs = 0;                            /* new match: look the players up afresh */
        was_menu = menu;
    }
}

/* ---- who scored? */
static int gs_wstr(uintptr_t obj, int32_t off, wchar_t *out, int cap) { TArr s; out[0] = 0; return rd(obj + off, &s, sizeof s) && s.num > 1 && s.num <= cap && s.ptr > 0x10000 && rd(s.ptr, out, (SIZE_T)s.num * 2) && !out[s.num - 1]; }
static uintptr_t gs_cand[6]; static int gs_ncand;
static int gs_find_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; uint32_t fl = 0;
    if (!gs_gs_cls) { static uintptr_t lastc; if (cls == lastc) return 0; lastc = cls; if (class_is(cls, "MP_GameState_C")) gs_gs_cls = cls; else return 0; }
    if (cls != gs_gs_cls || !rd(o + 8, &fl, 4) || (fl & 0x10)) return 0;
    if (gs_ncand < 6) gs_cand[gs_ncand++] = o;                    /* an old match's game state can linger for a while: collect them all, the live one is chosen below */
    return 0;
}
static int gs_read(uintptr_t gs, GsPl *out, int cap) {
    int n = 0; TArr a; int32_t pa = -1;
    if (!prop_off(find_prop(gs_gs_cls, "PlayerArray"), &pa) || !rd(gs + pa, &a, sizeof a) || a.num < 0 || a.num > 64 || (a.num && a.ptr < 0x10000)) return 0;
    for (int i = 0; i < a.num && n < cap; i++) { uintptr_t ps = 0; int32_t os = -1, og = -1, on = -1; GsPl *p = &out[n];
        if (!rdp(a.ptr + 8ULL * i, &ps) || !ps) continue;
        uintptr_t pc = obj_class(ps); memset(p, 0, sizeof *p); p->ps = ps;
        if (prop_off(find_prop(pc, "SteamID"), &os)) get_fstring(ps, os, p->sid, sizeof p->sid);
        if (prop_off(find_prop(pc, "Stats_Goals"), &og)) rd(ps + og, &p->goals, 4);
        if (!(prop_off(find_prop(pc, "PlayerName"), &on) && gs_wstr(ps, on, p->name, 40)) && prop_off(find_prop(pc, "PlayerNamePrivate"), &on)) gs_wstr(ps, on, p->name, 40);
        n++; }
    return n;
}
static int gs_ids(const GsPl *p, int n) { int k = 0; for (int i = 0; i < n; i++) if (p[i].sid[0]) k++; return k; }
static int gs_players(GsPl *out, int cap) {                           /* the players in this match: SteamID, name, goals */
    int n = 0;
    if (gs_gs && gs_gs_cls && obj_class(gs_gs) != gs_gs_cls) gs_gs = 0;
    if (gs_gs_cls && !class_is(gs_gs_cls, "MP_GameState_C")) { gs_gs_cls = 0; gs_gs = 0; }
    if (gs_gs) { n = gs_read(gs_gs, out, cap); if (n && gs_ids(out, n)) return n; gs_gs = 0; }      /* nobody with an id: that game state is from an earlier match */
    DWORD now = GetTickCount(); if (gs_miss >= 3 && now - gs_miss_t < 8000) return 0; gs_miss_t = now;
    gs_ncand = 0; for_each_object(gs_find_cb, NULL);
    static GsPl tmp[48]; int best = 0;
    for (int i = 0; i < gs_ncand; i++) { int m = gs_read(gs_cand[i], tmp, 48), k = gs_ids(tmp, m); if (k > best) { best = k; gs_gs = gs_cand[i]; } }
    if (!gs_gs) { gs_miss++; return 0; }
    gs_miss = 0; return gs_read(gs_gs, out, cap);
}
static void gs_snapshot(void) { gs_nsnap = gs_players(gs_snap, 48); }      /* called every few seconds in a match */
/* the effect to play for the goal that was just given */
static int gs_pick_fx(void) {
    static GsPl cur[48]; char who[20] = ""; const char *how = "unknown"; wchar_t want[48]; memcpy(want, g_goal_scorer, sizeof want); want[47] = 0;
    int n = 0;
    for (int tries = 0; tries < 5 && !who[0]; tries++) {             /* the name gives it at once; otherwise up to ~0.1 s for the goal count to arrive */
        n = gs_players(cur, 48);
        if (!tries && want[0]) { int best = -1; size_t bl = 0;       /* the goal message's name is, or starts with / contains, one player's name */
            for (int i = 0; i < n; i++) { size_t L = wcslen(cur[i].name); if (!L || !cur[i].sid[0]) continue;
                if (!_wcsicmp(cur[i].name, want)) { best = i; bl = 9999; break; }
                if (L >= 3 && L > bl && wcsstr(want, cur[i].name)) { best = i; bl = L; } }
            if (best >= 0) { snprintf(who, sizeof who, "%s", cur[best].sid); how = "name"; break; } }
        if (!tries) { int humans = 0, at = -1; for (int i = 0; i < n; i++) if (gs_is_sid(cur[i].sid)) { humans++; at = i; }      /* alone in the match: it can only be that player */
            if (humans == 1) { snprintf(who, sizeof who, "%s", cur[at].sid); how = "only player"; break; } }
        for (int i = 0; i < n && !who[0]; i++) { int before = -1; for (int k = 0; k < gs_nsnap; k++) if (gs_snap[k].ps == cur[i].ps) before = gs_snap[k].goals;
            if (before >= 0 && cur[i].goals > before && cur[i].sid[0]) { snprintf(who, sizeof who, "%s", cur[i].sid); how = "goal count"; } }
        if (!who[0]) Sleep(25);
    }
    if (!who[0]) { static int lg; if (lg++ < 6) { char line[400] = "", a[48]; int z; for (z = 0; want[z] && z < 47; z++) a[z] = want[z] < 128 ? (char)want[z] : '?'; a[z] = 0;
        for (int i = 0; i < n && i < 12; i++) { char nm[40]; int k = 0; for (; cur[i].name[k] && k < 39; k++) nm[k] = cur[i].name[k] < 128 ? (char)cur[i].name[k] : '?'; nm[k] = 0; size_t L = strlen(line); snprintf(line + L, sizeof line - L, "'%s' %s goals %d; ", nm, cur[i].sid, cur[i].goals); }
        logf_("goal effects: scorer not recognised. Goal message name '%s'. Players in the match (%d): %s", a, n, line); } }
    gs_snapshot(); snprintf(gs_last_scorer, sizeof gs_last_scorer, "%s", who);
    int fx = who[0] ? gs_lookup(who) : 0; char me[20]; gs_my_sid(me, sizeof me);
    if (who[0] && !fx && me[0] && !strcmp(who, me) && GT.saved) fx = GT.saved;                    /* my own goal: my choice, even if the server list is a minute old */
    { static int lg; if (lg++ < 40) logf_("goal effects: scorer %s (found by %s) -> effect %d%s", who[0] ? who : "?", how, fx ? fx : GS_DEFAULT_FX, fx ? "" : " (default)"); }
    return fx ? fx : GS_DEFAULT_FX;
}
/* ---- Clown: who gets it? The goalkeeper who was scored on = the opposing player nearest to where the ball went in.
 * Nobody else on the pitch (testing alone): the scorer. Returns the address of that player's position, 0 if nobody. */
static char gs_victim_why[96];
static int gs_is_char(uintptr_t cls) {                               /* SP_Character_C or a blueprint built on it */
    static uintptr_t yes[4], no[8]; static int ny, nn; static DWORD t0; DWORD now = GetTickCount();
    if (now - t0 > 20000) { t0 = now; memset(yes, 0, sizeof yes); memset(no, 0, sizeof no); }      /* classes are reloaded between matches */
    for (int i = 0; i < 4; i++) if (yes[i] && yes[i] == cls) return 1;
    for (int i = 0; i < 8; i++) if (no[i] && no[i] == cls) return 0;
    uintptr_t c = cls; int ok = 0; for (int d = 0; c && d < 6 && !ok; d++) { if (class_is(c, "SP_Character_C")) ok = 1; else if (!rdp(c + 0x40, &c)) break; }
    if (ok) yes[ny++ & 3] = cls; else no[nn++ & 7] = cls;
    return ok;
}
static uintptr_t gs_chars[64]; static int gs_nchars;
static uintptr_t gs_lastno;
static int gs_chars_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; uint32_t fl = 0;
    if (cls == gs_lastno) return 0;
    if (!gs_is_char(cls)) { gs_lastno = cls; return 0; }
    if (!rd(o + 8, &fl, 4) || (fl & 0x10)) return 0;
    for (int i = 0; i < gs_nchars; i++) if (gs_chars[i] == o) return 0;
    if (gs_nchars < 64) gs_chars[gs_nchars++] = o;
    return 0;
}
static uintptr_t gs_extra[16]; static int gs_nextra;
static void gs_chars_prewarm(void) {                                 /* effects thread, now and then while alone in a match: characters that aren't in the player list */
    static GsPl cur[48]; int n = gs_players(cur, 48), pawns = 0; int32_t op = -1;
    for (int i = 0; i < n; i++) { uintptr_t pawn = 0; if (prop_off(find_prop(obj_class(cur[i].ps), "PawnPrivate"), &op) && rdp(cur[i].ps + op, &pawn) && pawn) pawns++; }
    if (pawns > 1) { gs_nextra = 0; return; }
    gs_nchars = 0; gs_lastno = 0; for_each_object(gs_chars_cb, NULL);
    int k = 0; for (int i = 0; i < gs_nchars && k < 16; i++) gs_extra[k++] = gs_chars[i];
    gs_nextra = k;
}
static int gs_int(uintptr_t o, const char *prop) { int32_t off = -1, v = -1; if (prop_off(find_prop(obj_class(o), prop), &off)) rd(o + off, &v, 4); return v; }
static uintptr_t gs_pick_victim(const float P[3], int exclude_scorer) {
    static GsPl cur[48]; int n = gs_players(cur, 48), others = 0; uintptr_t scorer = 0; int32_t op = -1; float l[3];
    gs_victim_pawn = 0; gs_nchars = 0; gs_victim_why[0] = 0;
    for (int i = 0; i < n; i++) { uintptr_t pawn = 0;
        if (!prop_off(find_prop(obj_class(cur[i].ps), "PawnPrivate"), &op) || !rdp(cur[i].ps + op, &pawn) || !pawn || !gs_is_char(obj_class(pawn))) continue;
        if (exclude_scorer && gs_last_scorer[0] && !strcmp(cur[i].sid, gs_last_scorer)) scorer = pawn; else others++;
        if (gs_nchars < 64) gs_chars[gs_nchars++] = pawn; }
    if (!others) for (int i = 0; i < gs_nextra; i++) { uintptr_t c = gs_extra[i]; int dup = 0; for (int k = 0; k < gs_nchars; k++) if (gs_chars[k] == c) dup = 1;      /* nobody else in the player list: any other character on the pitch (noted beforehand) */
        if (!dup && gs_nchars < 64 && gs_is_char(obj_class(c))) gs_chars[gs_nchars++] = c; }
    int steam = exclude_scorer && scorer ? gs_int(scorer, "Team") : -99, pick = -1, rank = 0; float best = 0; char line[420] = "";
    for (int i = 0; i < gs_nchars; i++) { uintptr_t c = gs_chars[i];
        if (!gw_actor_loc(c, l)) continue;
        float d = (l[0] - P[0]) * (l[0] - P[0]) + (l[1] - P[1]) * (l[1] - P[1]); int team = gs_int(c, "Team");
        int r = c == scorer ? 1 : (steam != -99 && team == steam) ? 2 : (steam != -99 && gs_int(c, "Position") == 0) ? 4 : 3;      /* the other team's goalkeeper (position 0), else its nearest player, then a team mate, the scorer only if alone */
        { size_t L = strlen(line); if (L < 380) snprintf(line + L, sizeof line - L, "[team %d pos %d gk %d, %.0f m%s] ", team, gs_int(c, "Position"), gs_int(c, "GkState"), sqrtf(d) / 100, c == scorer ? ", scorer" : ""); }
        if (r > rank || (r == rank && d < best)) { rank = r; best = d; pick = i; } }
    { static int lg; if (lg++ < 12) logf_("clown: players on the pitch: %s", line[0] ? line : "none"); }
    if (pick < 0 || !gw_actor_loc(gs_chars[pick], l)) { snprintf(gs_victim_why, sizeof gs_victim_why, "nobody (no player found on the pitch) - shown at the goal spot"); return 0; }
    gs_victim_pawn = gs_chars[pick];
    snprintf(gs_victim_why, sizeof gs_victim_why, "%s, %.0f m from the goal spot", rank == 4 ? "the other team's goalkeeper" : rank == 3 ? (exclude_scorer ? "the other team's player nearest the goal (it has no keeper)" : "the player nearest the ball") : rank == 2 ? "a team mate (no opponent on the pitch)" : "the scorer (alone on the pitch)", sqrtf(best) / 100);
    return gw_last_loc_addr;
}
static void goalsync_start(void) { HANDLE t = CreateThread(NULL, 0, gs_thread, NULL, 0, NULL); if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); } }

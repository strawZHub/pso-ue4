/* stamina.h - stamina refills 5% faster, on every map.
 * Nothing in the game's own stamina logic is changed. A small worker watches each character's stamina value about
 * 20 times a second; whenever it has gone UP since the last look (= the game refilled it), 5% of that rise is
 * added on top. So whatever a map's or a position's refill speed is, it becomes that speed x 1.05. Draining is
 * untouched, the bar never goes past full, and a jump of more than a fifth of the bar (kick-off reset) is ignored.
 *   match server: every player's character (the server's value is what counts when it decides).
 *   player's game: only the player's own character (so what they feel matches the server at once).
 * The match server also notes once per match how fast stamina refills there, to compare maps. */
#define ST_BONUS 0.05f
typedef struct { uintptr_t ch; float last, acc; DWORD t0; } StEnt;
static StEnt st_tab[64]; static unsigned st_next; static float st_max; static int32_t st_off = -1; static uintptr_t st_cls; static int st_logged; static char st_map[96];
static void st_one(uintptr_t ch, int server) {
    uintptr_t cls = obj_class(ch); float s = 0; StEnt *e = NULL; DWORD now = GetTickCount();
    if (!cls) return;
    if (cls != st_cls) { int32_t o = -1; if (!prop_off(find_prop(cls, "Stamina"), &o) || o <= 0) return; st_cls = cls; st_off = o; }
    if (!rd(ch + st_off, &s, 4) || !(s >= 0 && s < 1e6f)) return;
    for (int i = 0; i < 64; i++) if (st_tab[i].ch == ch) { e = &st_tab[i]; break; }
    if (s > st_max) st_max = s;                                         /* the fullest bar seen so far = full */
    if (!e) { e = &st_tab[st_next++ & 63]; e->ch = ch; e->last = s; e->acc = 0; e->t0 = 0; return; }
    float d = s - e->last;
    if (d > 0 && d < st_max * 0.2f && s < st_max) {
        if (server && !st_logged) { if (!e->t0) { e->t0 = now; e->acc = 0; } else e->acc += d;
            if (now - e->t0 >= 3000) { st_logged = 1; logf_("stamina: on %s a full bar is %.2f and it refills %.3f per second (before the +5%%)", st_map[0] ? st_map : "this map", st_max, e->acc * 1000.f / (float)(now - e->t0)); } }
        float ns = s + d * ST_BONUS; if (ns > st_max) ns = st_max;
        if (wr_(ch + st_off, &ns, 4)) s = ns;
    } else e->t0 = 0;
    e->last = s;
}
static DWORD WINAPI stamina_server_worker(LPVOID u) {                    /* match server: all players */
    (void)u; if (!refl_init()) return 0;
    static GsPl pl[48]; static uintptr_t ps[48]; int n = 0; int32_t op = -1; uintptr_t pscls = 0;
    for (int tick = 0;; tick++) {
        Sleep(50);
        if (tick % 40 == 0) {                                            /* who is in the match: looked up every 2 s */
            n = gs_players(pl, 48); for (int i = 0; i < n; i++) ps[i] = pl[i].ps;
            if (n) { uintptr_t c = obj_class(ps[0]); if (c != pscls) { op = -1; if (prop_off(find_prop(c, "PawnPrivate"), &op)) pscls = c; else pscls = 0; } } }
        if (op < 0 || !pscls) continue;
        for (int i = 0; i < n; i++) { uintptr_t pawn = 0; if (obj_class(ps[i]) != pscls || !rdp(ps[i] + op, &pawn) || !pawn) continue; st_one(pawn, 1); }
    }
}
static DWORD WINAPI stamina_client_worker(LPVOID u) {                    /* a player's game: their own character */
    (void)u; if (!refl_init()) return 0;
    int32_t op = -1; uintptr_t pcc = 0;
    for (;;) {
        Sleep(50);
        if (g_in_menu) { Sleep(450); continue; }
        uintptr_t pc = GW.pc, pawn = 0, c;
        if (!pc || !GW.pc_cls || (c = obj_class(pc)) != GW.pc_cls) continue;
        if (c != pcc) { if (!prop_off(find_prop(c, "Pawn"), &op)) continue; pcc = c; }
        if (rdp(pc + op, &pawn) && pawn) st_one(pawn, 0);
    }
}
static void stamina_start(int server) { HANDLE t = CreateThread(NULL, 0, server ? stamina_server_worker : stamina_client_worker, NULL, 0, NULL); if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); } }

/* Reads the player's Steam info for Pro Soccer Online from their own Steam install (read only):
 *  - account + name:   registry ActiveUser / config\loginusers.vdf
 *  - avatar:           config\avatarcache\<SteamID64>.png
 *  - play time, last:  userdata\<id>\config\localconfig.vdf  ("Playtime" minutes, "LastPlayed" unix time)
 *  - achievements:     appcache\stats\UserGameStatsSchema_1583320.bin + UserGameStats_<id>_1583320.bin */
#define PSO_APPID "1583320"

typedef struct {
    int ok;                       /* found a Steam account */
    wchar_t name[128];
    wchar_t avatar_path[MAX_PATH * 2];
    long long minutes;            /* -1 = unknown */
    long long last_played;        /* unix time, 0 = unknown */
    int ach_have, ach_total;      /* total 0 = unknown */
} SteamInfo;
static SteamInfo g_steam;

static char *read_all(const wchar_t *path, long *len) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return NULL; }
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    b[n] = 0; fclose(f); *len = n;
    return b;
}

/* end of the { } block that starts at the first '{' after p (text VDF) */
static const char *vdf_block_end(const char *p) {
    p = strchr(p, '{'); if (!p) return NULL;
    int depth = 0; int inq = 0;
    for (; *p; p++) {
        if (*p == '"' && p[-1] != '\\') inq = !inq;
        if (inq) continue;
        if (*p == '{') depth++;
        else if (*p == '}' && --depth == 0) return p;
    }
    return NULL;
}
/* value of "key" "value" between p and end */
static int vdf_value(const char *p, const char *end, const char *key, char *out, int cap) {
    char k[96]; snprintf(k, sizeof k, "\"%s\"", key);
    size_t kl = strlen(k);
    for (const char *q = p; q && q < end; ) {
        const char *hit = strstr(q, k);
        if (!hit || hit >= end) return 0;
        /* case-insensitive keys exist (LastPlayed / lastplayed); try exact first */
        const char *v = hit + kl;
        while (*v == ' ' || *v == '\t') v++;
        if (*v == '"') {
            v++; int n = 0;
            while (*v && *v != '"' && n < cap - 1) out[n++] = *v++;
            out[n] = 0;
            return 1;
        }
        q = hit + kl;
    }
    return 0;
}

/* ---------------- binary KeyValues (appcache\stats) */
typedef struct KV { char name[64]; int type; long long i; char s[64]; struct KV *child, *next; } KV;
static KV *kv_parse(const unsigned char *b, long n, long *pos, int depth) {
    KV *head = NULL, **tail = &head;
    while (*pos < n && depth < 16) {
        int t = b[(*pos)++];
        if (t == 8 || t == 11) break;
        KV *e = calloc(1, sizeof(KV)); e->type = t;
        int k = 0; while (*pos < n && b[*pos]) { if (k < 63) e->name[k++] = (char)b[*pos]; (*pos)++; }
        (*pos)++;
        switch (t) {
        case 0: e->child = kv_parse(b, n, pos, depth + 1); break;
        case 1: { int j = 0; while (*pos < n && b[*pos]) { if (j < 63) e->s[j++] = (char)b[*pos]; (*pos)++; } (*pos)++;
                  e->i = atoll(e->s); break; }
        case 2: case 3: case 4: case 6: if (*pos + 4 > n) { free(e); return head; }
                  e->i = *(const int32_t *)(b + *pos); *pos += 4; break;
        case 5: while (*pos + 1 < n && (b[*pos] || b[*pos + 1])) *pos += 2; *pos += 2; break;
        case 7: case 10: if (*pos + 8 > n) { free(e); return head; }
                  e->i = *(const long long *)(b + *pos); *pos += 8; break;
        default: free(e); return head;     /* unknown type: stop safely */
        }
        *tail = e; tail = &e->next;
    }
    return head;
}
static KV *kv_get(KV *l, const char *name) { for (; l; l = l->next) if (!_stricmp(l->name, name)) return l; return NULL; }
static int kv_count(KV *l) { int n = 0; for (; l; l = l->next) n++; return n; }
static void kv_free(KV *l) { while (l) { KV *n = l->next; kv_free(l->child); free(l); l = n; } }

static int popcount32(unsigned v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

static void read_achievements(const wchar_t *steam, unsigned accountid) {
    wchar_t p[MAX_PATH * 2]; long n; long pos;
    swprintf(p, MAX_PATH * 2, L"%ls\\appcache\\stats\\UserGameStatsSchema_" PSO_APPID L".bin", steam);
    unsigned char *sch = (unsigned char *)read_all(p, &n);
    if (!sch) return;
    pos = 0; KV *root = kv_parse(sch, n, &pos, 0); free(sch);
    KV *app = root ? (kv_get(root, PSO_APPID) ? kv_get(root, PSO_APPID) : root) : NULL;
    KV *stats = app ? kv_get(app->child, "stats") : NULL;
    char achid[64][16]; int nach = 0, total = 0;
    for (KV *s = stats ? stats->child : NULL; s; s = s->next) {
        KV *ty = kv_get(s->child, "type"); KV *ti = kv_get(s->child, "type_int");
        long long t = ti ? ti->i : (ty ? ty->i : 0);
        KV *bits = kv_get(s->child, "bits");
        if ((t == 4 || t == 5) && bits) { total += kv_count(bits->child); if (nach < 64) snprintf(achid[nach++], 16, "%s", s->name); }
    }
    kv_free(root);
    if (!total) return;
    g_steam.ach_total = total;
    swprintf(p, MAX_PATH * 2, L"%ls\\appcache\\stats\\UserGameStats_%u_" PSO_APPID L".bin", steam, accountid);
    unsigned char *us = (unsigned char *)read_all(p, &n);
    if (!us) return;
    pos = 0; KV *u = kv_parse(us, n, &pos, 0); free(us);
    KV *cache = u ? (kv_get(u, "cache") ? kv_get(u, "cache")->child : u) : NULL;
    int have = 0;
    for (int i = 0; i < nach; i++) {
        KV *st = kv_get(cache, achid[i]);
        if (!st) continue;
        KV *d = kv_get(st->child, "data");
        if (d) have += popcount32((unsigned)d->i);
        else { KV *at = kv_get(st->child, "AchievementTimes"); if (at) have += kv_count(at->child); }
    }
    kv_free(u);
    g_steam.ach_have = have > total ? total : have;
}

static void read_steam_info(void) {
    g_steam.minutes = -1;
    wchar_t steam[MAX_PATH]; DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, steam, &sz)) return;
    for (wchar_t *c = steam; *c; c++) if (*c == L'/') *c = L'\\';
    DWORD active = 0; sz = sizeof active;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, NULL, &active, &sz);

    /* loginusers.vdf: pick the active account (or the most recent one) and its name */
    wchar_t p[MAX_PATH * 2]; long n;
    swprintf(p, MAX_PATH * 2, L"%ls\\config\\loginusers.vdf", steam);
    char *lu = read_all(p, &n);
    unsigned long long sid = 0; char name[128] = "";
    if (lu) {
        for (const char *q = lu; (q = strstr(q, "\"7656119")); ) {
            unsigned long long id = strtoull(q + 1, NULL, 10);
            const char *end = vdf_block_end(q);
            if (!end) break;
            char mr[8] = "", pn[128] = "";
            vdf_value(q, end, "MostRecent", mr, sizeof mr);
            vdf_value(q, end, "PersonaName", pn, sizeof pn);
            int match = active ? (unsigned)(id - 76561197960265728ULL) == active : mr[0] == '1';
            if (match || !sid) { sid = id; snprintf(name, sizeof name, "%s", pn); }
            if (match) break;
            q = end;
        }
        free(lu);
    }
    if (!sid) return;
    unsigned accountid = (unsigned)(sid - 76561197960265728ULL);
    g_steam.ok = 1;
    MultiByteToWideChar(CP_UTF8, 0, name[0] ? name : "Steam player", -1, g_steam.name, 128);
    swprintf(g_steam.avatar_path, MAX_PATH * 2, L"%ls\\config\\avatarcache\\%llu.png", steam, sid);

    /* localconfig.vdf: Playtime + LastPlayed for the game */
    swprintf(p, MAX_PATH * 2, L"%ls\\userdata\\%u\\config\\localconfig.vdf", steam, accountid);
    char *lc = read_all(p, &n);
    if (lc) {
        for (const char *q = lc; (q = strstr(q, "\"" PSO_APPID "\"")); q++) {
            const char *end = vdf_block_end(q);
            const char *brace = strchr(q, '{');
            if (!end || !brace) break;
            /* the app block must open right after the key */
            const char *t = q + strlen("\"" PSO_APPID "\"");
            while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') t++;
            if (t != brace) continue;
            char v[32];
            if (vdf_value(q, end, "Playtime", v, sizeof v)) g_steam.minutes = atoll(v);
            if (vdf_value(q, end, "LastPlayed", v, sizeof v)) g_steam.last_played = atoll(v);
            if (g_steam.minutes >= 0) break;
        }
        free(lc);
    }
    read_achievements(steam, accountid);
}

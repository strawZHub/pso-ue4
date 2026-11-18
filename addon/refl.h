/* Minimal UE 4.27 reflection for ProSoccerOnline-Win64-Shipping.exe (this exact build).
 * All reads go through ReadProcessMemory so a bad pointer can never crash the game. */
#define RVA_GOBJ_OBJECTS 0x4519250   /* FChunkedFixedUObjectArray::Objects (GUObjectArray+0x10) */
#define RVA_NAMEPOOL     0x44dcbc0   /* FNamePool */
#define EXE_SIZE_CHECK   75652096

static uintptr_t g_exe;

static int rdp(uintptr_t a, uintptr_t *out) { return a > 0x10000 && rd(a, out, 8); }

static int refl_init(void) {
    char path[MAX_PATH];
    g_exe = (uintptr_t)GetModuleHandleA(NULL);
    GetModuleFileNameA(NULL, path, MAX_PATH);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz = {0};
    if (f != INVALID_HANDLE_VALUE) { GetFileSizeEx(f, &sz); CloseHandle(f); }
    if (sz.QuadPart != EXE_SIZE_CHECK) { logf_("add-on: different game build, disabled"); return 0; }
    return 1;
}

/* FName -> text (ANSI names only; wide names are returned empty) */
static int name_str(uint32_t idx, uint32_t num, char *out, int cap) {
    out[0] = 0;
    uintptr_t block;
    if (!rdp(g_exe + RVA_NAMEPOOL + 0x10 + 8ULL * (idx >> 16), &block)) return 0;
    uintptr_t e = block + 2ULL * (idx & 0xFFFF);
    uint16_t hdr;
    if (!rd(e, &hdr, 2)) return 0;
    int len = hdr >> 6;
    if ((hdr & 1) || len <= 0 || len >= cap - 12) return 0;
    if (!rd(e + 2, out, len)) return 0;
    out[len] = 0;
    if (num) snprintf(out + len, cap - len, "_%u", num - 1);
    return 1;
}
static int obj_name(uintptr_t o, char *out, int cap) {
    uint32_t n[2];
    return rd(o + 0x18, n, 8) && name_str(n[0], n[1], out, cap);
}
static uintptr_t obj_class(uintptr_t o) { uintptr_t c = 0; rdp(o + 0x10, &c); return c; }

/* walk every live UObject; cb returns 1 to stop */
typedef int (*obj_cb)(uintptr_t obj, uintptr_t cls, void *ctx);
static void for_each_object(obj_cb cb, void *ctx) {
    static unsigned char items[65536 * 24];
    uintptr_t chunks; int32_t num;
    if (!rdp(g_exe + RVA_GOBJ_OBJECTS, &chunks) || !rd(g_exe + RVA_GOBJ_OBJECTS + 0x14, &num, 4)) return;
    if (num <= 0 || num > 4000000) return;
    for (int c = 0; c * 65536 < num; c++) {
        uintptr_t chunk;
        if (!rdp(chunks + 8ULL * c, &chunk)) continue;
        int n = num - c * 65536; if (n > 65536) n = 65536;
        if (!rd(chunk, items, (SIZE_T)n * 24)) continue;
        for (int i = 0; i < n; i++) {
            uintptr_t o = *(uintptr_t *)(items + i * 24);
            if (o < 0x10000) continue;
            uintptr_t cls = obj_class(o);
            if (cls && cb(o, cls, ctx)) return;
        }
    }
}

/* property lookup on a UClass (walks super classes). Returns FProperty* or 0 */
static uintptr_t find_prop(uintptr_t cls, const char *want) {
    char nm[256];
    for (int depth = 0; cls && depth < 32; depth++) {
        uintptr_t p = 0;
        rdp(cls + 0x50, &p);                         /* UStruct::ChildProperties */
        for (int k = 0; p && k < 4000; k++) {
            uint32_t n[2];
            if (rd(p + 0x28, n, 8) && name_str(n[0], n[1], nm, sizeof nm) && !strcmp(nm, want)) return p;
            if (!rdp(p + 0x20, &p)) break;           /* FField::Next */
        }
        uintptr_t sup = 0; rdp(cls + 0x40, &sup);    /* UStruct::SuperStruct */
        cls = sup;
    }
    return 0;
}
static int prop_off(uintptr_t prop, int32_t *off) { return prop && rd(prop + 0x4C, off, 4); }

/* set a bool UPROPERTY (bitfield) on an object */
static int set_bool(uintptr_t obj, uintptr_t prop) {
    int32_t off; uint8_t b[4], cur;
    if (!prop_off(prop, &off) || !rd(prop + 0x78, b, 4)) return 0;  /* FieldSize, ByteOffset, ByteMask, FieldMask */
    uintptr_t a = obj + off + b[1];
    if (!rd(a, &cur, 1)) return 0;
    if ((cur & b[3]) == b[3]) return 1;
    cur |= b[3];
    SIZE_T put = 0;
    return WriteProcessMemory(g_proc, (LPVOID)a, &cur, 1, &put) && put == 1;
}

/* read an FString UPROPERTY as ANSI */
static int get_fstring(uintptr_t obj, int32_t off, char *out, int cap) {
    TArr s; wchar_t w[64];
    out[0] = 0;
    if (!rd(obj + off, &s, sizeof s) || s.num <= 1 || s.num > 64 || !rd(s.ptr, w, s.num * 2)) return 0;
    int n = s.num - 1 < cap - 1 ? s.num - 1 : cap - 1;
    for (int i = 0; i < n; i++) out[i] = (char)w[i];
    out[n] = 0;
    return 1;
}

static int is_cdo(uintptr_t o) { char n[256]; return obj_name(o, n, sizeof n) && !strncmp(n, "Default__", 9); }
static int class_is(uintptr_t cls, const char *want) { char n[256]; return obj_name(cls, n, sizeof n) && !strcmp(n, want); }

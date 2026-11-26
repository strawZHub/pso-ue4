/* goalfx.h - goal celebrations placed IN THE WORLD, where the ball went in (test build).
 * The add-on reads the game's camera and the ball position and draws the effect in the right place and
 * perspective every frame, in a click-through see-through window over the game (own thread, half resolution).
 * Works in Borderless / Windowed (not in exclusive Fullscreen).
 * Test keys (game window active): F6 = next animation, F7 = replay. A real goal plays the selected one. */
#include <math.h>
#define GFX_N 9
static const float GFX_DUR[GFX_N] = { 1.5f, 1.6f, 2.0f, 2.8f, 2.8f, 4.0f, 2.3f, 3.2f, 3.8f };
static volatile int g_fx_3d;                                    /* Clown: the keeper's real hair was changed in the game (then the drawn head isn't needed) */
static float g_fx_T[3]; static int g_fx_T_ok;                   /* Clown: the head of the keeper who was scored on (followed every frame) */
typedef struct { float pos[3], X[3], Y[3], Z[3], focal; } GCam;           /* camera: position, forward/right/up, focal length in low-res pixels */

static void gcam_set(GCam *c, const float pos[3], float pitch, float yaw, float roll, float fov, int lw) {
    float k = 0.01745329252f, cp = cosf(pitch * k), sp = sinf(pitch * k), cy = cosf(yaw * k), sy = sinf(yaw * k), cr = cosf(roll * k), sr = sinf(roll * k);
    memcpy(c->pos, pos, 12);
    c->X[0] = cp * cy; c->X[1] = cp * sy; c->X[2] = sp;
    c->Y[0] = sr * sp * cy - cr * sy; c->Y[1] = sr * sp * sy + cr * cy; c->Y[2] = -sr * cp;
    c->Z[0] = -(cr * sp * cy + sr * sy); c->Z[1] = cy * sr - cr * sp * sy; c->Z[2] = cr * cp;
    c->focal = (lw * 0.5f) / tanf(fov * k * 0.5f);
}
/* Depth of the effect, per pixel. Besides its colour, every effect pixel notes how far from the camera the thing
 * drawn there is (the brightest thing, if several overlap). In the game that is compared with how far the game's own
 * picture is at that pixel, so a player or goalpost standing in front of a part of the effect hides that part.
 * Whatever is drawn next takes the depth of the point that was last placed in the world (gcam_proj); flat labels use 0
 * = always in front. Off (g_zb NULL) for menu pictures and when the game's depth can't be used. */
static uint16_t *g_zb; static uint8_t *g_zs; static uint16_t g_zq;
static void gfx_setz(float z) { g_zq = z <= 0 ? 0 : z >= 262000.f ? 65534 : (uint16_t)(z * 0.25f); }      /* 4 cm steps */
#define GFX_Z(idx, s) do { if (g_zb) { int s_ = (int)(s); if (s_ > 255) s_ = 255; if (s_ > 1 && s_ >= g_zs[idx]) { g_zs[idx] = (uint8_t)s_; g_zb[idx] = g_zq; } } } while (0)
static int gcam_proj(const GCam *c, int lw, int lh, const float p[3], float *sx, float *sy, float *depth) {
    float d[3] = { p[0] - c->pos[0], p[1] - c->pos[1], p[2] - c->pos[2] };
    float z = d[0] * c->X[0] + d[1] * c->X[1] + d[2] * c->X[2]; *depth = z;
    if (z < 20) return 0;
    gfx_setz(z);
    *sx = lw * 0.5f + (d[0] * c->Y[0] + d[1] * c->Y[1] + d[2] * c->Y[2]) / z * c->focal;
    *sy = lh * 0.5f - (d[0] * c->Z[0] + d[1] * c->Z[1] + d[2] * c->Z[2]) / z * c->focal;
    return 1;
}
static float gfx_h(unsigned i, unsigned k) {
    unsigned x = i * 2654435761u ^ (k * 2246822519u + 0x9E3779B9u);
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (x & 0xFFFFFF) / 16777216.0f;
}
static uint32_t gfx_px(float r, float g, float b, float a) {       /* premultiplied 0xAARRGGBB */
    if (!(a > 0.002f)) return 0;
    if (a > 1) a = 1;
    if (r > 1) r = 1; if (g > 1) g = 1; if (b > 1) b = 1;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    uint32_t A = (uint32_t)(a * 255 + .5f), R = (uint32_t)(r * a * 255 + .5f), G = (uint32_t)(g * a * 255 + .5f), B = (uint32_t)(b * a * 255 + .5f);
    return A << 24 | R << 16 | G << 8 | B;
}
static float gfx_atan2(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-9f) return 0;
    float a = mn / mx, s = a * a, r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}
static void gfx_dot(uint32_t *lo, int lw, int lh, float fx, float fy, float rad, float r, float g, float b, float br) {   /* soft additive dot */
    if (!(fx > -50 && fx < lw + 50 && fy > -50 && fy < lh + 50)) return;
    if (rad > 40) rad = 40;
    int x0 = (int)(fx - rad), x1 = (int)(fx + rad) + 1, y0 = (int)(fy - rad), y1 = (int)(fy + rad) + 1; float ir = 1 / (rad * rad);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float q = 1 - ((x - fx) * (x - fx) + (y - fy) * (y - fy)) * ir; if (q <= 0) continue;
        float w = q * q * br * 255; uint32_t p = lo[y * lw + x]; GFX_Z(y * lw + x, w);
        int R = (int)((p >> 16 & 255) + r * w), G = (int)((p >> 8 & 255) + g * w), B = (int)((p & 255) + b * w), A = (int)(p >> 24);
        if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
        int m = R > G ? R : G; if (B > m) m = B; if (m > A) A = m;
        lo[y * lw + x] = (uint32_t)A << 24 | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
    }
}
/* distance (cm, on the pitch) from the goal spot for every pixel of one row; <0 where the pixel doesn't look at the pitch */
static float gfx_near[4200];                                    /* 0..1: fades the pitch effects right under the camera, where they would fill the screen */
static void gfx_ground_row(const GCam *c, int lw, int lh, int y, const float P[3], float gz, float *out) {
    float sy = lh * 0.5f - y, b0 = c->X[0] * c->focal + c->Z[0] * sy, b1 = c->X[1] * c->focal + c->Z[1] * sy, b2 = c->X[2] * c->focal + c->Z[2] * sy;
    float h = gz - c->pos[2], ox = c->pos[0] - P[0], oy = c->pos[1] - P[1];
    for (int x = 0; x < lw; x++) {
        float sx = x - lw * 0.5f, dz = b2 + c->Y[2] * sx;
        float t = fabsf(dz) > 1e-6f ? h / dz : -1;
        if (t <= 0) { out[x] = -1; continue; }
        float hx = ox + (b0 + c->Y[0] * sx) * t, hy = oy + (b1 + c->Y[1] * sx) * t;
        float nf = (t * c->focal - 280) / 700; gfx_near[x] = nf < 0 ? 0 : nf > 1 ? 1 : nf;
        if (nf <= 0) { out[x] = -1; continue; }
        if (g_zb) { float zv = t * c->focal * 0.97f - 20; g_zb[y * lw + x] = zv < 4 ? 1 : zv >= 262000.f ? 65534 : (uint16_t)(zv * 0.25f); g_zs[y * lw + x] = 1; }      /* that spot on the pitch, a touch nearer so the pitch itself never hides what is drawn on it */
        out[x] = sqrtf(hx * hx + hy * hy);
    }
}
/* sparks thrown out of the goal spot in 3D (drawn on top of whatever is in the buffer) */
static void gfx_sparks(uint32_t *lo, int lw, int lh, const GCam *c, const float P[3], float gz, float time, unsigned n, unsigned seed,
                       float smin, float smax, float elmin, float elmax, float grav, float life, float size, float r, float g, float b) {
    if (time <= 0 || time >= life) return;
    float dr = (1 - expf(-2.0f * time)) / 2.0f, br = powf(1 - time / life, 1.3f);
    for (unsigned i = 0; i < n; i++) {
        float an = gfx_h(i, seed) * 6.2831853f, el = elmin + gfx_h(i, seed + 1) * (elmax - elmin), sp = smin + (smax - smin) * gfx_h(i, seed + 2);
        float p[3] = { P[0] + cosf(an) * cosf(el) * sp * dr, P[1] + sinf(an) * cosf(el) * sp * dr, P[2] + sinf(el) * sp * dr - grav * time * time };
        if (p[2] < gz + 3) p[2] = gz + 3;
        float x, y, z; if (!gcam_proj(c, lw, lh, p, &x, &y, &z)) continue;
        float tw = 0.75f + 0.25f * sinf(time * 38 + i * 1.7f);
        gfx_dot(lo, lw, lh, x, y, c->focal * 100 / z * size + 1.2f, r, g, b, br * tw);
    }
}
static void gfx_line(uint32_t *lo, int lw, int lh, float x0, float y0, float x1, float y1, float rad, float r, float g, float b, float br) {   /* glowing line */
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy), st = rad * 0.6f; if (st < 0.8f) st = 0.8f;
    int n = (int)(len / st) + 1; if (n > 400) n = 400;
    for (int i = 0; i <= n; i++) { float t = (float)i / n; gfx_dot(lo, lw, lh, x0 + dx * t, y0 + dy * t, rad, r, g, b, br); }
}
static void v_norm(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static void v_cross(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static void gfx_dot3(uint32_t *lo, int lw, int lh, const GCam *c, const float p[3], float rcm, float minpx, float r, float g, float b, float br) {   /* a glowing ball in the world */
    float x, y, z; if (!gcam_proj(c, lw, lh, p, &x, &y, &z)) return;
    gfx_dot(lo, lw, lh, x, y, c->focal * rcm / z + minpx, r, g, b, br);
}
/* the dragon's spine: a helix climbing out of the goal spot. u = 0 at the ground .. 1 at the top */
static void dr_path(float u, const float P[3], float gz, float time, float sc, float out[3]) {
    float uu = u < 0 ? 0 : u, ang = uu * 2.2f * 6.2831853f + 0.6f, rad = (270 - 120 * uu) * sc, h = 760 * powf(uu, 0.9f) * sc;
    out[0] = P[0] + cosf(ang) * rad; out[1] = P[1] + sinf(ang) * rad; out[2] = gz + h + 15 + 20 * sc * sinf(u * 36 - time * 7) * (uu > 0.03f);
}
static void gfx_bolt(uint32_t *lo, int lw, int lh, const GCam *c, const float a[3], const float b[3], unsigned seed, float disp, float thick, float br) {   /* one jagged lightning bolt */
    enum { N = 33 }; float p[N][3]; memcpy(p[0], a, 12); memcpy(p[N - 1], b, 12);
    for (int step = N - 1; step > 1; step /= 2, disp *= 0.55f)
        for (int i = step / 2; i < N; i += step) for (int k = 0; k < 3; k++)
            p[i][k] = (p[i - step / 2][k] + p[i + step / 2][k]) * 0.5f + (gfx_h(i * 7 + k, seed) - 0.5f) * 2 * disp * (k == 2 ? 0.35f : 1);
    float x0 = 0, y0 = 0, z0; int ok0 = gcam_proj(c, lw, lh, p[0], &x0, &y0, &z0);
    for (int i = 1; i < N; i++) { float x1, y1, z1; int ok1 = gcam_proj(c, lw, lh, p[i], &x1, &y1, &z1);
        if (ok0 && ok1) { float m = c->focal * 100 / z1;
            gfx_line(lo, lw, lh, x0, y0, x1, y1, m * 0.22f * thick + 2.6f, 0.35f, 0.5f, 1.0f, br * 0.45f);         /* blue glow */
            gfx_line(lo, lw, lh, x0, y0, x1, y1, m * 0.06f * thick + 1.1f, 1.0f, 1.0f, 1.0f, br); }                 /* white core */
        x0 = x1; y0 = y1; ok0 = ok1; }
}
static void gfx_ell(uint32_t *lo, int lw, int lh, float cx, float cy, float rx, float ry, float R, float G, float B, float A) {   /* solid (not glowing) ellipse, smooth edge */
    if (rx < 0.3f || ry < 0.3f || !(cx > -400 && cx < lw + 400 && cy > -400 && cy < lh + 400)) return;
    int x0 = (int)(cx - rx - 1), x1 = (int)(cx + rx + 1), y0 = (int)(cy - ry - 1), y1 = (int)(cy + ry + 1); float mn = rx < ry ? rx : ry;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float dx = (x - cx) / rx, dy = (y - cy) / ry, cov = (1 - sqrtf(dx * dx + dy * dy)) * mn + 0.5f; if (cov <= 0) continue; if (cov > 1) cov = 1; cov *= A;
        uint32_t p = lo[y * lw + x]; float k = 1 - cov; GFX_Z(y * lw + x, cov * 255);
        uint32_t a = (uint32_t)((p >> 24) * k + cov * 255), r = (uint32_t)((p >> 16 & 255) * k + R * cov * 255), g2 = (uint32_t)((p >> 8 & 255) * k + G * cov * 255), b = (uint32_t)((p & 255) * k + B * cov * 255);
        lo[y * lw + x] = (a > 255 ? 255 : a) << 24 | (r > 255 ? 255 : r) << 16 | (g2 > 255 ? 255 : g2) << 8 | (b > 255 ? 255 : b);
    }
}
/* text pictures made once from a Windows font ("HA HA", "CLOWN"); stamped in any size / colour */
#define GFX_NSPR 2
static const uint8_t *g_spr[GFX_NSPR]; static int g_spr_w[GFX_NSPR], g_spr_h[GFX_NSPR];
static void gfx_stamp1(uint32_t *lo, int lw, int lh, int k, float cx, float cy, float hpx, float R, float G, float B, float A) {
    if (k < 0 || k >= GFX_NSPR || !g_spr[k] || hpx < 3) return;
    float sc = hpx / g_spr_h[k], w = g_spr_w[k] * sc, inv = 1 / sc; int x0 = (int)(cx - w / 2), y0 = (int)(cy - hpx / 2), x1 = (int)(cx + w / 2), y1 = (int)(cy + hpx / 2);
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < lh; y++) { int sy = (int)((y - (cy - hpx / 2)) * inv); if (sy < 0 || sy >= g_spr_h[k]) continue;
        for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < lw; x++) { int sx = (int)((x - (cx - w / 2)) * inv); if (sx < 0 || sx >= g_spr_w[k]) continue;
            float cov = g_spr[k][sy * g_spr_w[k] + sx] / 255.f * A; if (cov <= 0.01f) continue;
            uint32_t p = lo[y * lw + x]; float q = 1 - cov; GFX_Z(y * lw + x, cov * 255);
            uint32_t a = (uint32_t)((p >> 24) * q + cov * 255), r = (uint32_t)((p >> 16 & 255) * q + R * cov * 255), g2 = (uint32_t)((p >> 8 & 255) * q + G * cov * 255), b = (uint32_t)((p & 255) * q + B * cov * 255);
            lo[y * lw + x] = (a > 255 ? 255 : a) << 24 | (r > 255 ? 255 : r) << 16 | (g2 > 255 ? 255 : g2) << 8 | (b > 255 ? 255 : b); } }
}
static void gfx_stamp(uint32_t *lo, int lw, int lh, int k, float cx, float cy, float hpx, float R, float G, float B, float A) {   /* comic style: thick black outline, then the colour */
    float o = hpx * 0.07f; if (o < 1.5f) o = 1.5f;
    for (int i = 0; i < 8; i++) gfx_stamp1(lo, lw, lh, k, cx + cosf(i * 0.785f) * o, cy + sinf(i * 0.785f) * o, hpx, 0.05f, 0.03f, 0.08f, A);
    gfx_stamp1(lo, lw, lh, k, cx, cy, hpx, R, G, B, A);
}
static void gfx_tri(uint32_t *lo, int lw, int lh, float ax, float ay, float bx, float by, float cx, float cy, float R, float G, float B, float A) {   /* solid triangle */
    float mnx = ax < bx ? ax : bx, mxx = ax > bx ? ax : bx, mny = ay < by ? ay : by, mxy = ay > by ? ay : by;
    if (cx < mnx) mnx = cx; if (cx > mxx) mxx = cx; if (cy < mny) mny = cy; if (cy > mxy) mxy = cy;
    int x0 = (int)mnx, x1 = (int)mxx + 1, y0 = (int)mny, y1 = (int)mxy + 1; if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
    uint32_t px = gfx_px(R, G, B, 1); float q = 1 - A;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float w0 = (bx - ax) * (y - ay) - (by - ay) * (x - ax), w1 = (cx - bx) * (y - by) - (cy - by) * (x - bx), w2 = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
        if (!((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))) continue;
        GFX_Z(y * lw + x, A * 255);
        if (A >= 0.99f) lo[y * lw + x] = px; else { uint32_t p = lo[y * lw + x];
            lo[y * lw + x] = (uint32_t)((p >> 24) * q + A * 255) << 24 | (uint32_t)((p >> 16 & 255) * q + R * A * 255) << 16 | (uint32_t)((p >> 8 & 255) * q + G * A * 255) << 8 | (uint32_t)((p & 255) * q + B * A * 255); } }
}
/* one frame of animation `id` (1..GFX_N) at `time` seconds. P = goal spot (world), gz = pitch height */
static void gfx_render(int id, float time, float dt, int first, uint32_t *lo, int lw, int lh, const GCam *c, const float P[3], float gz) {
    float dur = GFX_DUR[id - 1], t = time / dur; if (t > 1) t = 1;
    float sx = 0, sy = 0, depth = 0; int vis = gcam_proj(c, lw, lh, P, &sx, &sy, &depth);
    float m = vis ? c->focal * 100 / depth : 0;                    /* low-res pixels per metre at the goal spot */
    static float row[4200]; if (lw > 4200) return;
    if (g_zb) { size_t np = (size_t)lw * lh; if (first || (id != 5 && id != 7)) { memset(g_zb, 0xFF, np * 2); memset(g_zs, 0, np); }
                else for (size_t i = 0; i < np; i++) g_zs[i] = (uint8_t)(g_zs[i] * 7 / 8); }      /* the two effects that leave trails keep theirs, fading */
    float ks = vis ? depth / 1500 : 1; if (ks < 0.45f) ks = 0.45f; if (ks > 2.4f) ks = 2.4f;      /* far camera: bigger effect, so it reads the same on screen */
    if (id == 1) {                                                  /* Shockwave: a ring racing across the pitch + a flash at the ball */
        float e = 1 - (1 - t) * (1 - t) * (1 - t), R = 120 + e * 2800, w = 70 + 190 * e, fade = powf(1 - t, 1.4f);
        float R2 = R * 0.6f, fl = 1 - t * 3.5f, fr = m * 4.5f; if (fl < 0) fl = 0;
        for (int y = 0; y < lh; y++) { uint32_t *o = lo + y * lw; gfx_ground_row(c, lw, lh, y, P, gz, row);
            for (int x = 0; x < lw; x++) {
                float d = row[x], a = 0, wh = 0, q;
                if (d >= 0) {
                    q = 1 - fabsf(d - R) / w; if (q > 0) { q *= q; a += q * 0.9f * fade; wh += q * q; }
                    q = 1 - fabsf(d - R - w * 0.25f) / (w * 0.22f); if (q > 0) { a += q * fade; wh += q; }                 /* thin bright leading edge */
                    q = 1 - fabsf(d - R * 0.3f) / (w * 0.4f); if (q > 0) a += q * q * 0.25f * fade;
                    q = 1 - fabsf(d - R2) / (w * 0.6f); if (q > 0) { q *= q; a += q * 0.4f * fade; }
                    a *= gfx_near[x]; wh *= gfx_near[x];
                }
                if (fl > 0 && vis && fr > 1) { float dx = x - sx, dy = y - sy; q = 1 - sqrtf(dx * dx + dy * dy) / fr; if (q > 0) { q = q * q * fl; a += q * 0.9f; wh += q; } }
                if (wh > 1) wh = 1;
                o[x] = gfx_px(0.30f + 0.70f * wh, 0.72f + 0.28f * wh, 1.0f, a);
            } }
        gfx_sparks(lo, lw, lh, c, P, gz, time, 90, 40, 700, 2600, 0.05f, 0.75f, 260, dur * 0.85f, 0.045f, 0.75f, 0.95f, 1.0f);
    } else if (id == 2) {                                           /* Gold burst: turning rays of light from the ball */
        static float rays[1024]; static int init;
        if (!init) { init = 1; for (int i = 0; i < 1024; i++) { float v = 0.5f + 0.5f * sinf(i * 6.2831853f * 16 / 1024); rays[i] = v * v * (0.6f + 0.4f * sinf(i * 6.2831853f * 5 / 1024 + 1)); } }
        float u = m * 13; if (u < lh * 0.22f) u = lh * 0.22f; if (u > lh * 1.3f) u = lh * 1.3f;
        float iu = 1 / u, e = 1 - (1 - t) * (1 - t) * (1 - t), Rm = 0.2f + 0.95f * e, in = t * 9; if (in > 1) in = 1;
        float env = vis ? in * powf(1 - t, 1.4f) : 0; int rot = (int)(time * 45);
        float gR = 100 + e * 1500, gw = 50 + 110 * e;
        for (int y = 0; y < lh; y++) { float dy = (y - sy) * iu; uint32_t *o = lo + y * lw; gfx_ground_row(c, lw, lh, y, P, gz, row);
            for (int x = 0; x < lw; x++) {
                float dx = (x - sx) * iu, d = sqrtf(dx * dx + dy * dy), a = 0, core = 0, q = 1 - d / Rm;
                if (q > 0) a = rays[((int)(gfx_atan2(dy, dx) * 162.97466f) + rot) & 1023] * q * 1.25f;
                q = 1 - d / 0.32f; if (q > 0) { core = q * q; a += core * 0.85f; }
                if (row[x] >= 0) { float ga = 0; q = 1 - fabsf(row[x] - gR) / gw; if (q > 0) ga += q * q * 0.8f; q = 1 - row[x] / 380; if (q > 0) ga += q * 0.5f; a += ga * gfx_near[x]; }
                o[x] = gfx_px(1.0f, 0.80f + 0.2f * core, 0.32f + 0.6f * core, a * env);
            } }
        gfx_sparks(lo, lw, lh, c, P, gz, time, 110, 50, 500, 2000, 0.25f, 1.45f, 330, dur * 0.9f, 0.05f, 1.0f, 0.85f, 0.4f);
    } else if (id == 3) {                                           /* Light pillar: a beam shoots up from the spot, rings pulse on the pitch */
        float in = t * 6; if (in > 1) in = 1; float env = in * powf(1 - t, 1.2f), grow = t * 4; if (grow > 1) grow = 1;
        float B[3] = { P[0], P[1], gz }, T[3] = { P[0], P[1], gz + 3200 * grow }, bx, by, tx, ty, bd, td;
        int okb = gcam_proj(c, lw, lh, B, &bx, &by, &bd), okt = gcam_proj(c, lw, lh, T, &tx, &ty, &td), beam = okb && okt;
        float vx = tx - bx, vy = ty - by, vv = vx * vx + vy * vy + 1e-3f, w = m * 1.6f; if (w < 3) w = 3;
        float ph = fmodf(time * 1.6f, 1.0f), R = 150 + ph * 1500, rw = 60 + 120 * ph, rf = 1 - ph;
        for (int y = 0; y < lh; y++) { uint32_t *o = lo + y * lw; gfx_ground_row(c, lw, lh, y, P, gz, row);
            for (int x = 0; x < lw; x++) {
                float a = 0, core = 0, q, d = row[x];
                if (d >= 0) { q = 1 - fabsf(d - R) / rw; if (q > 0) a += q * q * 0.7f * rf; q = 1 - d / 420; if (q > 0) a += q * q * 0.8f; a *= gfx_near[x]; }
                if (beam) {
                    float px = x - bx, py = y - by, s = (px * vx + py * vy) / vv; if (s < 0) s = 0; if (s > 1) s = 1;
                    float ex = px - vx * s, ey = py - vy * s, wd = w * (1 - 0.55f * s);
                    q = 1 - sqrtf(ex * ex + ey * ey) / (wd * 2.6f);
                    if (q > 0) { float g = q * q * (1 - s * s * 0.8f); a += g * 0.75f; core = q * q * q * q; a += core * 0.6f; }
                }
                if (core > 1) core = 1;
                o[x] = gfx_px(0.35f + 0.65f * core, 0.75f + 0.25f * core, 1.0f, a * env);
            } }
        for (unsigned i = 0; i < 70; i++) {                        /* sparkles spiralling up around the beam */
            float an = gfx_h(i, 60) * 6.2831853f + time * (2.5f + 2 * gfx_h(i, 61)), rr = 70 + 150 * gfx_h(i, 62), hz = fmodf(gfx_h(i, 63) + time * (0.45f + 0.4f * gfx_h(i, 64)), 1.0f);
            float p[3] = { P[0] + cosf(an) * rr, P[1] + sinf(an) * rr, gz + hz * 2600 * grow }, x, y, z;
            if (!gcam_proj(c, lw, lh, p, &x, &y, &z)) continue;
            gfx_dot(lo, lw, lh, x, y, c->focal * 100 / z * 0.05f + 1.2f, 0.7f, 0.95f, 1.0f, env * (1 - hz) * (0.6f + 0.4f * sinf(time * 30 + i)));
        }
    } else if (id == 4) {                                           /* Confetti cannon: bursts out of the goal spot and floats down */
        static const float pal[6][3] = { {1,0.25f,0.3f}, {1,0.8f,0.2f}, {0.25f,0.85f,0.45f}, {0.25f,0.6f,1}, {0.75f,0.4f,1}, {1,1,1} };
        memset(lo, 0, (size_t)lw * lh * 4);
        float out = (dur - time) / 0.5f; if (out > 1) out = 1; if (out < 0) out = 0;
        float dr = (1 - expf(-1.7f * time)) / 1.7f;
        for (unsigned i = 0; i < 300; i++) {
            float an = gfx_h(i, 1) * 6.2831853f, el = 0.35f + gfx_h(i, 2) * 1.15f, sp = (900 + 2000 * gfx_h(i, 3)) * ks;
            float p[3] = { P[0] + cosf(an) * cosf(el) * sp * dr, P[1] + sinf(an) * cosf(el) * sp * dr, P[2] + sinf(el) * sp * dr - 170 * ks * time * time };
            if (p[2] < gz + 4) p[2] = gz + 4;
            float x, y, z; if (!gcam_proj(c, lw, lh, p, &x, &y, &z)) continue;
            float pm = c->focal * 100 / z, rot = time * (4 + 6 * gfx_h(i, 4)), hw = pm * 0.11f * fabsf(cosf(rot)) + 0.5f, hh = pm * 0.11f + 0.5f;
            if (hw > 30) hw = 30; if (hh > 30) hh = 30;
            const float *cl = pal[i % 6]; float sh = 0.6f + 0.4f * fabsf(cosf(rot)); uint32_t pxl = gfx_px(cl[0] * sh, cl[1] * sh, cl[2] * sh, 0.95f * out);
            int x0 = (int)(x - hw), x1 = (int)(x + hw), y0 = (int)(y - hh), y1 = (int)(y + hh);
            if (x1 < 0 || y1 < 0 || x0 >= lw || y0 >= lh) continue;
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
            for (int yy = y0; yy <= y1; yy++) for (int xx = x0; xx <= x1; xx++) { lo[yy * lw + xx] = pxl; GFX_Z(yy * lw + xx, 240); }
        }
        if (vis && time < 0.3f) gfx_dot(lo, lw, lh, sx, sy, m * 2.2f + 3, 1, 1, 0.9f, 1 - time / 0.3f);                   /* the pop */
    } else if (id == 5) {                                           /* Fireworks: four bursts in the air above the goal spot */
        static const float bt[4] = { 0.25f, 0.5f, 0.75f, 1.05f }, bo[4][3] = { {0,0,330}, {-270,190,470}, {270,-190,540}, {0,0,680} };
        static const float bc[4][3] = { {1,0.78f,0.3f}, {0.3f,0.85f,1}, {1,0.4f,0.7f}, {1,1,1} };
        if (first) memset(lo, 0, (size_t)lw * lh * 4);
        else { unsigned k = (unsigned)(expf(-dt * 6.5f) * 256); unsigned char *b = (unsigned char *)lo; size_t n = (size_t)lw * lh * 4; for (size_t i = 0; i < n; i++) b[i] = (unsigned char)(b[i] * k >> 8); }
        for (int bi = 0; bi < 4; bi++) {
            float tau = time - bt[bi], x, y, z;
            if (tau < 0 && tau > -0.25f) {                          /* the rocket climbing from the goal spot */
                float s = (1 + tau / 0.25f) * ks * 1.5f, p[3] = { P[0] + bo[bi][0] * s, P[1] + bo[bi][1] * s, gz + bo[bi][2] * s };
                if (gcam_proj(c, lw, lh, p, &x, &y, &z)) gfx_dot(lo, lw, lh, x, y, c->focal * 100 / z * 0.07f + 1.5f, 1, 0.95f, 0.8f, 0.9f);
                continue;
            }
            if (tau < 0 || tau > 1.4f) continue;
            float br = powf(1 - tau / 1.4f, 1.4f), dr = (1 - expf(-2.4f * tau)) / 2.4f;
            float fs = ks * 1.5f;
            if (tau < 0.12f) { float p[3] = { P[0] + bo[bi][0] * fs, P[1] + bo[bi][1] * fs, gz + bo[bi][2] * fs };
                if (gcam_proj(c, lw, lh, p, &x, &y, &z)) gfx_dot(lo, lw, lh, x, y, c->focal * 100 / z * 0.9f + 3, 1, 1, 1, 1 - tau / 0.12f); }
            for (unsigned i = 0; i < 110; i++) {
                float an = gfx_h(i, 10 + bi) * 6.2831853f, cz = gfx_h(i, 30 + bi) * 2 - 1, sz = sqrtf(1 - cz * cz), sp = (420 + 420 * gfx_h(i, 20 + bi)) * fs;
                float p[3] = { P[0] + bo[bi][0] * fs + cosf(an) * sz * sp * dr, P[1] + bo[bi][1] * fs + sinf(an) * sz * sp * dr, gz + bo[bi][2] * fs + cz * sp * dr - 90 * fs * tau * tau };
                if (!gcam_proj(c, lw, lh, p, &x, &y, &z)) continue;
                float tw = tau > 0.5f ? 0.55f + 0.45f * sinf(time * 45 + i * 2.3f) : 1;
                int alt = (i & 3) == 0;                                 /* every 4th spark is white */
                gfx_dot(lo, lw, lh, x, y, c->focal * 100 / z * 0.06f * fs + 1.5f, alt ? 1 : bc[bi][0], alt ? 1 : bc[bi][1], alt ? 1 : bc[bi][2], br * tw);
            }
        }
    } else if (id == 6) {                                           /* Dragon: a fire dragon spirals out of the pitch, roars and breathes fire */
        float sc = (ks < 0.6f ? 0.6f : ks > 1.6f ? 1.6f : ks) * 1.7f, in = time * 3; if (in > 1) in = 1;
        float out = (dur - time) / 0.6f; if (out > 1) out = 1; if (out < 0) out = 0; float env = in * out;
        float fl = 0.85f + 0.15f * sinf(time * 23);
        for (int y = 0; y < lh; y++) { uint32_t *o = lo + y * lw; gfx_ground_row(c, lw, lh, y, P, gz, row);    /* the burning ring it rises from */
            for (int x = 0; x < lw; x++) { float d = row[x], a = 0, q; if (d < 0) { o[x] = 0; continue; }
                q = 1 - fabsf(d - 330 * sc) / (70 * sc); if (q > 0) a += q * q * 0.9f; q = 1 - d / (330 * sc); if (q > 0) a += q * 0.35f;
                o[x] = gfx_px(1.0f, 0.42f + 0.3f * a, 0.08f, a * env * fl * gfx_near[x]); } }
        float uh = time / 2.3f; if (uh > 1) uh = 1; uh = uh * uh * (3 - 2 * uh); uh += 0.006f * sinf(time * 4);
        enum { NS = 96 }; float prev[3], pos[3], T[3], S[3], U[3], up[3] = { 0, 0, 1 };
        for (int i = NS - 1; i >= 0; i--) {                         /* body, tail first */
            float u = uh - i * 0.0102f; if (u < 0) continue;
            dr_path(u, P, gz, time, sc, pos); dr_path(u + 0.004f, P, gz, time, sc, prev);
            T[0] = prev[0] - pos[0]; T[1] = prev[1] - pos[1]; T[2] = prev[2] - pos[2]; v_norm(T); v_cross(T, up, S); v_norm(S); v_cross(S, T, U);
            float f = (float)i / NS, r = (i == 0 ? 60 : i < 5 ? 40 : f < 0.55f ? 47 : 47 * (1 - (f - 0.55f) / 0.45f) + 5) * sc, sh = 0.72f + 0.28f * sinf(i * 1.9f);
            gfx_dot3(lo, lw, lh, c, pos, r, 1.2f, 1.0f, 0.22f + 0.2f * (1 - f), 0.04f, 0.42f * env * sh);                 /* red-orange hide */
            gfx_dot3(lo, lw, lh, c, pos, r * 0.5f, 0.8f, 1.0f, 0.8f, 0.3f, 0.5f * env);                                 /* glowing core */
            if (i > 2 && (i & 1)) { float sp[3] = { pos[0] + U[0] * r * 1.25f, pos[1] + U[1] * r * 1.25f, pos[2] + U[2] * r * 1.25f }; gfx_dot3(lo, lw, lh, c, sp, r * 0.34f, 0.7f, 1.0f, 0.7f, 0.15f, 0.7f * env); }   /* back spikes */
            if (i == 0) {                                           /* the head */
                float hp[3];
#define HP(a, b, cc) (hp[0] = pos[0] + T[0] * (a) * sc + S[0] * (b) * sc + U[0] * (cc) * sc, hp[1] = pos[1] + T[1] * (a) * sc + S[1] * (b) * sc + U[1] * (cc) * sc, hp[2] = pos[2] + T[2] * (a) * sc + S[2] * (b) * sc + U[2] * (cc) * sc)
                HP(48, 0, -4); gfx_dot3(lo, lw, lh, c, hp, 36 * sc, 1, 1.0f, 0.3f, 0.05f, 0.6f * env);                    /* snout */
                HP(88, 0, -10); gfx_dot3(lo, lw, lh, c, hp, 22 * sc, 1, 1.0f, 0.4f, 0.08f, 0.6f * env);
                for (int s = -1; s <= 1; s += 2) {
                    HP(24, 27 * s, 22); gfx_dot3(lo, lw, lh, c, hp, 11 * sc, 1.2f, 0.6f, 1.0f, 1.0f, 1.0f * env);          /* eyes */
                    for (int k = 1; k <= 6; k++) { HP(-16 * k, (24 + 5 * k) * s, 30 + 15 * k); gfx_dot3(lo, lw, lh, c, hp, (11 - 1.4f * k) * sc, 0.8f, 1.0f, 0.85f, 0.4f, 0.8f * env); }     /* horns */
                    for (int k = 1; k <= 9; k++) { HP(80 - 9 * k, (20 + 17 * k) * s, -8 * k + 10 * sinf(time * 6 + k * 0.7f)); gfx_dot3(lo, lw, lh, c, hp, 4.5f * sc, 0.7f, 1.0f, 0.75f, 0.3f, 0.6f * env); } /* whiskers */
                }
                float f0 = time - 2.35f;                            /* fire breath */
                if (f0 > 0) { float D[3] = { T[0] * 0.75f, T[1] * 0.75f, T[2] * 0.75f - 0.75f }; v_norm(D);                 /* down onto the pitch */ HP(105, 0, -12); float mo[3] = { hp[0], hp[1], hp[2] };
                    for (unsigned k = 0; k < 150; k++) { float st = k * 0.006f; if (f0 < st) break; float a = fmodf(f0 - st, 0.85f) / 0.85f; if (f0 - st > 1.25f) continue;
                        float dist = (1500 * a - 520 * a * a) * sc, spx = (gfx_h(k, 71) - 0.5f) * 520 * a * sc, spy = (gfx_h(k, 72) - 0.5f) * 520 * a * sc;
                        float fp[3] = { mo[0] + D[0] * dist + S[0] * spx + U[0] * spy, mo[1] + D[1] * dist + S[1] * spx + U[1] * spy, mo[2] + D[2] * dist + S[2] * spx + U[2] * spy };
                        float br = powf(1 - a, 1.2f) * out;
                        gfx_dot3(lo, lw, lh, c, fp, (16 + 95 * a) * sc, 1.2f, 1.0f, a < 0.25f ? 0.95f : 0.75f - 0.6f * a, a < 0.2f ? 0.6f : 0.1f * (1 - a), 0.5f * br); } }
#undef HP
            }
        }
        gfx_sparks(lo, lw, lh, c, P, gz, time, 70, 80, 300 * sc, 1300 * sc, 0.7f, 1.5f, 120, 1.6f, 0.04f, 1.0f, 0.6f, 0.15f);       /* embers as it bursts out */
    } else if (id == 7) {                                           /* Thunder strike: lightning hits the goal spot three times */
        static const float bt[3] = { 0.05f, 0.45f, 0.8f };
        if (first) memset(lo, 0, (size_t)lw * lh * 4);
        else { unsigned k = (unsigned)(expf(-dt * 9.0f) * 256); unsigned char *b = (unsigned char *)lo; size_t n = (size_t)lw * lh * 4; for (size_t i = 0; i < n; i++) b[i] = (unsigned char)(b[i] * k >> 8); }
        float G[3] = { P[0], P[1], gz + 5 };
        for (int bi = 0; bi < 3; bi++) {
            float tau = time - bt[bi], big = bi == 2 ? 1.6f : 1; if (tau < 0 || tau > 0.34f) continue;
            if (tau - dt < 0) { unsigned v = bi == 2 ? 95 : 60; unsigned char *b = (unsigned char *)lo; size_t n = (size_t)lw * lh * 4; for (size_t i = 0; i < n; i++) { unsigned s = b[i] + v; b[i] = (unsigned char)(s > 255 ? 255 : s); } }   /* the sky lights up */
            float br = expf(-tau * 7) * (0.65f + 0.35f * gfx_h((unsigned)(tau * 40), 90 + bi)); unsigned seed = 100 + bi * 17 + (unsigned)(tau * 14);
            float A[3] = { P[0] + (gfx_h(bi, 95) - 0.5f) * 1500, P[1] + (gfx_h(bi, 96) - 0.5f) * 1500, gz + 3000 };
            gfx_bolt(lo, lw, lh, c, A, G, seed, 380, big, br);
            if (bi == 2) { float A2[3] = { P[0] - (gfx_h(7, 95) - 0.5f) * 1800, P[1] + (gfx_h(8, 96) - 0.5f) * 1800, gz + 3000 }; gfx_bolt(lo, lw, lh, c, A2, G, seed + 5, 420, 1.2f, br * 0.9f); }
            for (int k = 0; k < 3; k++) {                           /* side branches */
                float s = 0.3f + 0.2f * k, B0[3] = { A[0] + (G[0] - A[0]) * s, A[1] + (G[1] - A[1]) * s, A[2] + (G[2] - A[2]) * s };
                float B1[3] = { B0[0] + (gfx_h(k, 97 + bi) - 0.5f) * 1300, B0[1] + (gfx_h(k, 98 + bi) - 0.5f) * 1300, B0[2] - 500 - 400 * gfx_h(k, 99) };
                gfx_bolt(lo, lw, lh, c, B0, B1, seed + 31 * k, 150, 0.5f, br * 0.55f); }
            gfx_dot3(lo, lw, lh, c, G, 260 * big, 3, 0.8f, 0.9f, 1.0f, br);                                                    /* impact glow */
        }
        for (int bi = 0; bi < 3; bi++) gfx_sparks(lo, lw, lh, c, G, gz, time - bt[bi], bi == 2 ? 70 : 35, 110 + bi * 3, 500, 1900, 0.1f, 1.1f, 420, 0.7f, 0.035f, 0.7f, 0.85f, 1.0f);
    } else if (id == 8) {                                           /* Black hole: opens above the goal spot, swallows the light, then blows up */
        float sc = (ks < 0.6f ? 0.6f : ks > 1.6f ? 1.6f : ks) * 1.7f, C[3] = { P[0], P[1], gz + 230 * sc }, cx, cy, cz;
        const float t1 = 2.1f, t2 = 2.4f; float grow = time / 1.2f; if (grow > 1) grow = 1; grow = grow * grow * (3 - 2 * grow);
        float col = time < t1 ? 0 : time < t2 ? (time - t1) / (t2 - t1) : 1, Rcm = 150 * sc * grow * (1 - col), tau = time - t2;
        int okc = gcam_proj(c, lw, lh, C, &cx, &cy, &cz); float mc = okc ? c->focal / cz : 0, R = mc * Rcm;
        float A[3] = { c->Y[0], c->Y[1], c->Y[2] }, B[3] = { c->Z[0] * 0.40f + c->X[0] * 0.92f, c->Z[1] * 0.40f + c->X[1] * 0.92f, c->Z[2] * 0.40f + c->X[2] * 0.92f };       /* the disc always leans towards the viewer, so it reads as a ring */
        if (tau > 0) {                                              /* blast wave across the pitch */
            float e = tau / (dur - t2), e3 = 1 - (1 - e) * (1 - e) * (1 - e), Rg = 100 + e3 * 3000, w = 80 + 220 * e3, fade = powf(1 - e, 1.3f);
            for (int y = 0; y < lh; y++) { uint32_t *o = lo + y * lw; gfx_ground_row(c, lw, lh, y, P, gz, row);
                for (int x = 0; x < lw; x++) { float d = row[x], a = 0, wh = 0, q; if (d < 0) { o[x] = 0; continue; }
                    q = 1 - fabsf(d - Rg) / w; if (q > 0) { q *= q; a += q * fade; wh = q * q; } q = 1 - fabsf(d - Rg * 0.55f) / (w * 0.6f); if (q > 0) a += q * q * 0.4f * fade;
                    o[x] = gfx_px(0.6f + 0.4f * wh, 0.35f + 0.65f * wh, 1.0f, a * gfx_near[x]); } }
        } else memset(lo, 0, (size_t)lw * lh * 4);
        for (int pass = 0; pass < 2 && tau <= 0; pass++) {          /* matter spiralling in: behind the hole first, then the hole, then in front */
            for (unsigned i = 0; i < 320; i++) {
                float fr = fmodf(time * 0.45f + gfx_h(i, 121), 1.0f), rho = (1.15f + 2.6f * gfx_h(i, 122)) * 150 * sc * (1 - 0.8f * fr) * (0.25f + 0.75f * grow) * (1 - col * 0.95f);
                float th = gfx_h(i, 123) * 6.2831853f + time * (2.2f + 2.6f * fr * fr * 3), cs = cosf(th) * rho, sn = sinf(th) * rho;
                float p[3] = { C[0] + A[0] * cs + B[0] * sn, C[1] + A[1] * cs + B[1] * sn, C[2] + A[2] * cs + B[2] * sn }, x, y, z;
                if (!gcam_proj(c, lw, lh, p, &x, &y, &z) || (z > cz) != (pass == 0)) continue;
                float k = fr, br = (0.25f + 0.75f * k) * grow * (0.7f + 0.3f * col * 3);
                gfx_dot(lo, lw, lh, x, y, c->focal * (7 + 9 * (1 - k)) * sc / z + 1.2f, 0.55f + 0.45f * k, 0.3f + 0.7f * k * k, 1.0f, br);
            }
            if (pass == 0 && okc && R > 0.7f) {                    /* the hole itself: pure black with a thin ring of bent light */
                int x0 = (int)(cx - R * 2.7f), x1 = (int)(cx + R * 2.7f), y0 = (int)(cy - R * 2.7f), y1 = (int)(cy + R * 2.7f);
                if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
                gfx_setz(cz);
                for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
                    float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / R, q; uint32_t pp = lo[y * lw + x]; if (d < 1.1f) GFX_Z(y * lw + x, 250);
                    float pr = (pp >> 16 & 255) / 255.f, pg = (pp >> 8 & 255) / 255.f, pb = (pp & 255) / 255.f, pa = (pp >> 24) / 255.f;
                    float hole = d < 0.92f ? 1 : d < 1.04f ? (1.04f - d) / 0.12f : 0;
                    pr *= 1 - hole; pg *= 1 - hole; pb *= 1 - hole; pa = pa * (1 - hole) + hole * 0.96f;
                    q = 1 - fabsf(d - 1.07f) / 0.085f; if (q > 0) { q *= q; pr += q * 0.95f; pg += q * 0.85f; pb += q; if (q > pa) pa = q; }
                    if (d > 1) { q = 1 - (d - 1) / 1.6f; if (q > 0) { q = q * q * 0.32f; pr += q * 0.55f; pg += q * 0.3f; pb += q; if (q > pa) pa = q; } }
                    if (pr > pa) pa = pr; if (pg > pa) pa = pg; if (pb > pa) pa = pb; if (pa > 1) pa = 1;
                    lo[y * lw + x] = (uint32_t)(pa * 255) << 24 | (uint32_t)((pr > 1 ? 1 : pr) * 255) << 16 | (uint32_t)((pg > 1 ? 1 : pg) * 255) << 8 | (uint32_t)((pb > 1 ? 1 : pb) * 255);
                }
            }
        }
        if (tau > 0) {                                              /* the blast */
            if (tau < 0.3f && okc) gfx_dot(lo, lw, lh, cx, cy, mc * 900 * sc * (0.3f + tau / 0.3f) + 6, 1, 0.95f, 1, 1 - tau / 0.3f);
            gfx_sparks(lo, lw, lh, c, C, gz, tau, 150, 130, 900 * sc, 3200 * sc, -0.5f, 1.3f, 260, dur - t2, 0.05f, 0.75f, 0.5f, 1.0f);
        }
    } else {                                                        /* Clown: the keeper who let it in gets a clown head, balloons and confetti */
        static const float rb[6][3] = { {1,0.2f,0.2f}, {1,0.6f,0.1f}, {1,0.9f,0.15f}, {0.25f,0.8f,0.3f}, {0.2f,0.5f,1}, {0.7f,0.3f,0.95f} };
        memset(lo, 0, (size_t)lw * lh * 4);
        float H[3] = { P[0], P[1], gz + 175 }; if (g_fx_T_ok) memcpy(H, g_fx_T, 12);
        float hx, hy, hz;
        float out = (dur - time) / 0.4f; if (out > 1) out = 1; if (out < 0) out = 0;
        int draw_head = !g_fx_3d;                                    /* real 3D clown hair on the character: only the arrow, word and laughter are drawn */
        if (gcam_proj(c, lw, lh, H, &hx, &hy, &hz)) {               /* the clown head, over the keeper's own */
            float pop = time / 0.35f; if (pop > 1) pop = 1; pop -= 1; pop = 1 + 2.7f * pop * pop * pop + 1.7f * pop * pop;
            gfx_setz(0);                                                 /* the arrow, the word and the laughter are labels: never hidden */
            float R = c->focal * 36 / hz; if (R < lh * 0.042f) R = lh * 0.042f; if (R > lh * 0.3f) R = lh * 0.3f; R *= pop;
            float tl = 0.16f * sinf(time * 9), cs = cosf(tl), sn = sinf(tl), A = draw_head ? out : 0;
#define CL(ox, oy, rx, ry, r, g, b) gfx_ell(lo, lw, lh, hx + ((ox) * cs - (oy) * sn) * R, hy + ((ox) * sn + (oy) * cs) * R, (rx) * R, (ry) * R, r, g, b, A)
            for (int i = 0; i < 11; i++) { float a = 3.45f + i * 0.253f, bob = 1 + 0.05f * sinf(time * 12 + i); const float *cl = rb[i % 6];      /* rainbow hair */
                CL(cosf(a) * 1.06f * bob, sinf(a) * 1.06f * bob, 0.46f, 0.46f, cl[0], cl[1], cl[2]); }
            for (int i = 0; i < 6; i++) { float a = 3.9f + i * 0.33f; const float *cl = rb[(i + 3) % 6]; CL(cosf(a) * 1.45f, sinf(a) * 1.35f, 0.3f, 0.3f, cl[0], cl[1], cl[2]); }
            CL(-0.34f, 1.22f, 0.3f, 0.2f, 0.95f, 0.15f, 0.2f); CL(0.34f, 1.22f, 0.3f, 0.2f, 0.95f, 0.15f, 0.2f); CL(0, 1.22f, 0.13f, 0.13f, 1, 0.85f, 0.2f);          /* bow tie */
            CL(0, 0, 1, 1, 0.98f, 0.96f, 0.93f);                                                                                              /* white face */
            CL(-0.58f, 0.22f, 0.2f, 0.2f, 1, 0.6f, 0.65f); CL(0.58f, 0.22f, 0.2f, 0.2f, 1, 0.6f, 0.65f);                                          /* cheeks */
            for (int s = -1; s <= 1; s += 2) { CL(0.36f * s, -0.26f, 0.15f, 0.26f, 0.2f, 0.45f, 0.95f); CL(0.36f * s, -0.22f, 0.07f, 0.09f, 0.05f, 0.05f, 0.1f);      /* eyes */
                for (int k = 0; k < 5; k++) CL((0.2f + 0.08f * k) * s, -0.62f - 0.05f * sinf(k * 0.785f) , 0.05f, 0.04f, 0.15f, 0.1f, 0.1f); }                    /* eyebrows */
            for (int k = 0; k <= 14; k++) { float a = 0.35f + k * 0.174f; CL(cosf(a) * 0.56f, 0.12f + sinf(a) * 0.5f, 0.1f, 0.1f, 0.9f, 0.1f, 0.15f); }          /* big red grin */
            for (int k = 2; k <= 12; k++) { float a = 0.35f + k * 0.174f; CL(cosf(a) * 0.5f, 0.12f + sinf(a) * 0.42f, 0.045f, 0.045f, 1, 1, 1); }
            float honk = 0; for (int k = 0; k < 2; k++) { float th = time - (0.1f + k * 0.35f); if (th > 0 && th < 0.18f) honk = sinf(th / 0.18f * 3.14159f); }
            CL(0, 0.04f, 0.23f * (1 + 0.5f * honk), 0.23f * (1 + 0.5f * honk), 0.95f, 0.1f, 0.12f); CL(-0.07f, -0.03f, 0.07f, 0.06f, 1, 0.75f, 0.75f);     /* the nose (honks) */
#undef CL
            /* the arrow, the word and the laughter: small, and kept above / beside the player so he stays visible */
            float pm = c->focal / hz, S = pm * 30; if (S < lh * 0.026f) S = lh * 0.026f; if (S > lh * 0.14f) S = lh * 0.14f; S *= pop;
            {   /* a bouncing red arrow pointing down at them, with the word over it */
                float gap = draw_head ? R * 1.3f : (pm * 40 > S * 0.8f ? pm * 40 : S * 0.8f);
                float bob = fabsf(sinf(time * 7)) * S * 0.35f, aw = S * 0.75f, ah = S * 0.8f, in = time / 0.25f; if (in > 1) in = 1;
                float tipy = hy - gap - bob, top = tipy - ah * 1.9f, A2 = out * in;
                gfx_tri(lo, lw, lh, hx - aw * 1.12f, top + ah - S * 0.06f, hx + aw * 1.12f, top + ah - S * 0.06f, hx, tipy + S * 0.1f, 0.05f, 0.03f, 0.08f, A2);        /* outline */
                gfx_tri(lo, lw, lh, hx - aw * 0.52f, top - S * 0.06f, hx + aw * 0.52f, top - S * 0.06f, hx + aw * 0.52f, top + ah, 0.05f, 0.03f, 0.08f, A2);
                gfx_tri(lo, lw, lh, hx - aw * 0.52f, top - S * 0.06f, hx - aw * 0.52f, top + ah, hx + aw * 0.52f, top + ah, 0.05f, 0.03f, 0.08f, A2);
                gfx_tri(lo, lw, lh, hx - aw, top + ah, hx + aw, top + ah, hx, tipy, 1, 0.15f, 0.2f, A2);                                                                  /* head of the arrow */
                gfx_tri(lo, lw, lh, hx - aw * 0.42f, top, hx + aw * 0.42f, top, hx + aw * 0.42f, top + ah + 1, 1, 0.15f, 0.2f, A2);                                       /* its stem */
                gfx_tri(lo, lw, lh, hx - aw * 0.42f, top, hx - aw * 0.42f, top + ah + 1, hx + aw * 0.42f, top + ah + 1, 1, 0.15f, 0.2f, A2);
                gfx_stamp(lo, lw, lh, 1, hx, top - S * 0.75f, S * 1.05f * (1 + 0.06f * sinf(time * 14)), 1, 1, 1, A2);                                                    /* CLOWN */
            }
            for (int i = 0; i < 8; i++) {                           /* laughter popping up to the left and right, above shoulder height */
                float st = 0.45f + i * 0.36f, tl2 = time - st; if (tl2 < 0 || tl2 > 0.75f) continue;
                float a = (i & 1 ? 5.65f : 3.25f) + 0.5f * gfx_h(i, 151), d = S * (2.9f + 1.2f * gfx_h(i, 152)) + (draw_head ? R : 0), sc2 = tl2 < 0.12f ? tl2 / 0.12f * 1.25f : 1.25f - 0.25f * (tl2 - 0.12f > 0.1f ? 1 : (tl2 - 0.12f) / 0.1f);
                float fa = tl2 > 0.5f ? (0.75f - tl2) / 0.25f : 1; const float *cl = rb[(i * 5 + 1) % 6];
                gfx_stamp(lo, lw, lh, 0, hx + cosf(a) * d * 1.3f, hy + sinf(a) * d * 0.8f - tl2 * S * 0.6f, S * (0.6f + 0.3f * gfx_h(i, 153)) * sc2, i % 3 == 0 ? 1 : cl[0], i % 3 == 0 ? 0.92f : cl[1], i % 3 == 0 ? 0.2f : cl[2], out * fa);
            }
        }
    }
}
static uint32_t gfx_avg(uint32_t a, uint32_t b) { return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1); }
static void gfx_up(const uint32_t *lo, int lw, int lh, uint32_t *out, int W, int H, uint32_t *tmp) {   /* 2x smooth upscale */
    for (int y = 0; y < H; y++) {
        int sy = y >> 1; const uint32_t *a = lo + sy * lw, *r = a;
        if ((y & 1) && sy + 1 < lh) { const uint32_t *b = a + lw; for (int x = 0; x < lw; x++) tmp[x] = gfx_avg(a[x], b[x]); r = tmp; }
        uint32_t *o = out + (size_t)y * W;
        for (int x = 0; x < W; x++) { int sx = x >> 1; o[x] = ((x & 1) && sx + 1 < lw) ? gfx_avg(r[sx], r[sx + 1]) : r[sx]; }
    }
}

/* ---- sound: each animation's sound is made by the add-on itself (no sound files), 32 kHz mono */
#define GFX_SR 32000
#include "clown_pcm.h"
static float gfx_nz(unsigned *s) { *s = *s * 1664525u + 1013904223u; return (int)((*s >> 9) & 0xFFFF) / 32768.0f - 1; }
static unsigned char *gfx_make_wav(int id, unsigned *bytes) {
    if (id == 9) {                                                  /* the clown is a real recording (see clown_pcm.h), not made here */
        unsigned dsz = (unsigned)CLOWN_PCM_N * 2, v; unsigned char *w = (unsigned char *)malloc(44 + dsz); if (!w) return NULL;
        for (unsigned i = 0; i < CLOWN_PCM_N; i++) { int s = CLOWN_PCM[i] * 11 / 40;      /* the recording, at about the same loudness as the others */ w[44 + i * 2] = (unsigned char)(s & 255); w[45 + i * 2] = (unsigned char)((s >> 8) & 255); }
        memcpy(w, "RIFF", 4); v = 36 + dsz; memcpy(w + 4, &v, 4); memcpy(w + 8, "WAVEfmt ", 8); v = 16; memcpy(w + 16, &v, 4);
        w[20] = 1; w[21] = 0; w[22] = 1; w[23] = 0; v = GFX_SR; memcpy(w + 24, &v, 4); v = GFX_SR * 2; memcpy(w + 28, &v, 4); w[32] = 2; w[33] = 0; w[34] = 16; w[35] = 0;
        memcpy(w + 36, "data", 4); memcpy(w + 40, &dsz, 4); *bytes = 44 + dsz; return w;
    }
    float len = GFX_DUR[id - 1] + 0.4f; int n = (int)(len * GFX_SR);
    float *b = (float *)calloc(n, sizeof(float)); unsigned char *w = (unsigned char *)malloc(44 + (size_t)n * 2);
    if (!b || !w) { free(b); free(w); return NULL; }
    unsigned sd = 12345u + id; float lp = 0, lp2 = 0, ph = 0, ph2 = 0, ck = 0; const float TAU = 6.2831853f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / GFX_SR, o = 0, nz = gfx_nz(&sd);
        if (id == 1) {                                              /* deep boom + rushing air */
            ph += TAU * (38 + 95 * expf(-t * 9)) / GFX_SR; o += sinf(ph) * expf(-t * 3.0f) * 0.9f;
            float cut = 0.03f + 0.10f * expf(-t * 3); lp += (nz - lp) * cut; lp2 += (lp - lp2) * cut;
            o += lp2 * 3.0f * (t < 0.04f ? t / 0.04f : expf(-(t - 0.04f) * 2.6f));
        } else if (id == 2) {                                       /* bright rising chime */
            static const float fr[5] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f };      /* an octave lower than before: warm, not piercing */
            for (int k = 0; k < 5; k++) { float s = t - k * 0.055f; if (s < 0) continue;
                o += (sinf(TAU * fr[k] * s) + 0.35f * sinf(TAU * fr[k] * 2.76f * s) * expf(-s * 9)) * expf(-s * 3.6f) * 0.22f; }
            o += sinf(TAU * 85 * t) * expf(-t * 13) * 0.7f;
            lp += (nz - lp) * 0.6f; o += (nz - lp) * 0.05f * expf(-t * 5);
        } else if (id == 3) {                                       /* energy beam powering up, then humming out */
            float up = t / 0.45f; if (up > 1) up = 1;
            ph += TAU * (150 * powf(5.0f, up)) / GFX_SR; ph2 += TAU * (150 * powf(5.0f, up) * 1.505f) / GFX_SR;
            float env = (t < 0.03f ? t / 0.03f : 1) * expf(-t * 1.5f);
            o += (sinf(ph) + 0.35f * sinf(2 * ph) + 0.3f * sinf(ph2)) * env * 0.4f;
            o += sinf(TAU * 1175 * t) * (0.5f + 0.5f * sinf(TAU * 11 * t)) * 0.03f * env;
            o += sinf(TAU * 70 * t) * expf(-t * 9) * 0.6f;
        } else if (id == 4) {                                       /* party popper: pop, then paper crackle */
            lp += (nz - lp) * 0.35f; o += lp * expf(-t * 38) * 1.6f + sinf(TAU * 160 * t) * expf(-t * 24) * 0.8f;
            float pr = t > 0.05f && t < 1.6f ? 0.006f * (1 - t / 1.6f) : 0;
            if (gfx_nz(&sd) * 0.5f + 0.5f < pr * 0.6f) ck = 0.2f + 0.15f * (gfx_nz(&sd) * 0.5f + 0.5f);
            o += (nz - lp) * ck; ck *= 0.985f;
        } else if (id == 5) {                                       /* fireworks: whistle up, bang, crackle - four times */
            static const float bt[4] = { 0.25f, 0.5f, 0.75f, 1.05f };
            lp += (nz - lp) * 0.08f; lp2 += (lp - lp2) * 0.08f;
            for (int k = 0; k < 4; k++) { float s = t - bt[k];
                if (s < 0 && s > -0.25f) { float u = 1 + s / 0.25f; o += sinf(TAU * (600 + 500 * u) * (s + 0.25f)) * 0.03f * u; }
                else if (s >= 0 && s < 1.4f) {
                    o += lp2 * 5.5f * expf(-s * 6.5f) + sinf(TAU * (55 + 40 * expf(-s * 12)) * s) * expf(-s * 8) * 0.55f;
                    if (s > 0.3f && s < 1.2f && gfx_nz(&sd) * 0.5f + 0.5f < 0.0025f) ck = 0.16f;
                } }
            o += (nz - lp) * ck; ck *= 0.975f;
        } else if (id == 6) {                                       /* dragon: rushing climb, growl, then a long roar with fire */
            static float ph3, ph4, lp3;
            if (i == 0) ph3 = ph4 = lp3 = 0;
            float climb = t < 2.3f ? t / 2.3f : 1, r = t - 2.3f;
            lp += (nz - lp) * (0.02f + 0.05f * climb); o += lp * 2.2f * climb * (t < 2.3f ? 1 : expf(-r * 4)) * (0.75f + 0.25f * sinf(TAU * 3.1f * t));                 /* wind of the climb */
            ph += TAU * (44 + 30 * climb) / GFX_SR; float saw = fmodf(ph / TAU, 1.0f) * 2 - 1; lp2 += (saw - lp2) * 0.12f;
            o += lp2 * 0.5f * climb * (0.6f + 0.4f * sinf(TAU * 23 * t)) * (t < 2.3f ? 1 : expf(-r * 6));                                                             /* growl */
            if (r > 0) { float env = (r < 0.07f ? r / 0.07f : 1) * (r < 1.0f ? 1 : expf(-(r - 1.0f) * 3.2f)), f = 62 + 70 * expf(-r * 1.6f);
                ph2 += TAU * f / GFX_SR; ph3 += TAU * f * 1.51f / GFX_SR; ph4 += TAU * 29 / GFX_SR;
                float v = (fmodf(ph2 / TAU, 1.0f) * 2 - 1) + 0.6f * (fmodf(ph3 / TAU, 1.0f) * 2 - 1) + 0.5f * sinf(ph2 * 0.5f);
                float rough = 0.55f + 0.45f * (fmodf(ph4 / TAU, 1.0f) < 0.5f ? 1 : -0.2f);
                lp3 += (v * rough - lp3) * 0.22f; float roar = lp3 * 1.6f; roar = roar / (1 + fabsf(roar));                                                          /* the roar */
                o += roar * 1.2f * env + (nz - lp) * 0.14f * env * (0.7f + 0.3f * sinf(TAU * 17 * t))                                                                 /* fire hiss */
                   + sinf(TAU * 52 * r) * expf(-r * 7) * 0.9f; }
        } else if (id == 7) {                                       /* thunder: three cracks, each followed by a rolling rumble */
            static const float bt[3] = { 0.05f, 0.45f, 0.8f }; static float rm;
            if (i == 0) rm = 0;
            lp += (nz - lp) * 0.035f; lp2 += (lp - lp2) * 0.035f; rm += (gfx_nz(&sd) - rm) * 0.0006f;
            for (int k = 0; k < 3; k++) { float s = t - bt[k], big = k == 2 ? 1.5f : 1; if (s < 0) continue;
                o += nz * expf(-s * 34) * 0.55f * big + (nz - lp) * expf(-s * 11) * 0.22f * big                                                                          /* crack */
                   + lp2 * 9 * big * expf(-s * 2.0f) * (0.55f + 14 * fabsf(rm)) + sinf(TAU * (46 + 20 * expf(-s * 6)) * s) * expf(-s * 4.5f) * 0.8f * big; }         /* rumble */
        } else if (id == 8) {                                       /* black hole: a drone that builds, everything gets pulled in, a beat of silence, then the blast */
            const float t1 = 2.1f, t2 = 2.4f; float up = t / t1; if (up > 1) up = 1;
            if (t < t2) { float sw = t < t1 ? 1 : 1 - (t - t1) / (t2 - t1) * 0.3f;
                o += (sinf(TAU * 41 * t) + sinf(TAU * 43.3f * t) + 0.6f * sinf(TAU * 82.4f * t)) * 0.45f * up * up * sw;
                lp += (nz - lp) * (0.01f + 0.09f * up * up); o += lp * 2.0f * up * (0.6f + 0.4f * sinf(TAU * (5 + 14 * up) * t)) * sw;                                 /* swirl */
                if (t > t1) { float s = (t - t1) / (t2 - t1); ph += TAU * (180 * powf(9.0f, s)) / GFX_SR; o += sinf(ph) * 0.5f * s; } }                                /* the last pull */
            else { float s = t - t2; if (s > 0.05f) { s -= 0.05f; lp2 += (nz - lp2) * 0.05f;
                ph2 += TAU * (28 + 50 * expf(-s * 5)) / GFX_SR; o += sinf(ph2) * expf(-s * 2.6f) * 1.6f + lp2 * 5 * expf(-s * 4) + nz * expf(-s * 40) * 0.6f; } }
        } else {                                                    /* clown: two nose honks, then the sad trombone */
            for (int k = 0; k < 2; k++) { float s = t - (0.1f + k * 0.35f); if (s < 0 || s > 0.2f) continue;
                float f = 350 - 260 * s, e = sinf(s / 0.2f * 3.14159f); ph += TAU * f / GFX_SR;
                float sq = fmodf(ph / TAU, 1.0f) < 0.5f ? 1.0f : -1.0f; lp += (sq - lp) * 0.3f; o += (lp + 0.4f * sinf(ph * 3)) * e * 0.8f; }
            static const float nf[4] = { 293.66f, 277.18f, 261.63f, 246.94f }, ns[4] = { 0.95f, 1.33f, 1.71f, 2.09f }, nl[4] = { 0.34f, 0.34f, 0.34f, 1.25f };
            for (int k = 0; k < 4; k++) { float s = t - ns[k]; if (s < 0 || s > nl[k]) continue;
                float bend = k == 3 ? 1 - 0.06f * (s / nl[k]) + 0.012f * sinf(TAU * 5.5f * s) * (s / nl[k]) : 1, e = (s < 0.03f ? s / 0.03f : 1) * (nl[k] - s < 0.08f ? (nl[k] - s) / 0.08f : 1);
                ph2 += TAU * nf[k] * bend / GFX_SR; float saw = fmodf(ph2 / TAU, 1.0f) * 2 - 1;
                float wah = 0.06f + 0.10f * (0.5f + 0.5f * sinf(TAU * (k == 3 ? 3.2f : 2.6f) * s - 1.2f)); lp2 += (saw - lp2) * wah; o += lp2 * 1.6f * e; }
        }
        b[i] = o;
    }
    /* Finishing, the same for every effect so none of them sticks out: the hiss and sharp edges are filtered off, the very
     * deep rumble is thinned, every sound is brought to the same quiet-ish loudness (the game's own goal sound stays on top),
     * peaks are rounded instead of clipped, and each sound eases in and fades out over its last half second. */
    { float l1 = 0, l2 = 0, h1 = 0, hp = 0; const float a = 0.52f /* ~3.7 kHz */, hb = 0.0085f /* ~45 Hz */;
      for (int i = 0; i < n; i++) { l1 += (b[i] - l1) * a; l2 += (l1 - l2) * a; h1 += (l2 - h1) * hb; hp = l2 - h1; b[i] = hp; } }
    double e2 = 0; for (int i = 0; i < n; i++) e2 += (double)b[i] * b[i];
    float rms = (float)sqrt(e2 / (n > 0 ? n : 1)); if (rms < 1e-6f) rms = 1e-6f;
    float g = 0.034f / rms, lim = 0.26f; int fin = GFX_SR / 125, fade = GFX_SR / 2;
    for (int i = 0; i < n; i++) { float v = b[i] * g; v = lim * tanhf(v / lim);
        if (i < fin) v *= (float)i / fin; if (i > n - fade) v *= (float)(n - i) / fade;
        int s = (int)(v * 32767); w[44 + i * 2] = (unsigned char)(s & 255); w[45 + i * 2] = (unsigned char)((s >> 8) & 255); }
    free(b);
    unsigned dsz = (unsigned)n * 2, v;
    memcpy(w, "RIFF", 4); v = 36 + dsz; memcpy(w + 4, &v, 4); memcpy(w + 8, "WAVEfmt ", 8); v = 16; memcpy(w + 16, &v, 4);
    w[20] = 1; w[21] = 0; w[22] = 1; w[23] = 0; v = GFX_SR; memcpy(w + 24, &v, 4); v = GFX_SR * 2; memcpy(w + 28, &v, 4); w[32] = 2; w[33] = 0; w[34] = 16; w[35] = 0;
    memcpy(w + 36, "data", 4); memcpy(w + 40, &dsz, 4);
    *bytes = 44 + dsz; return w;
}

#ifndef GFX_TEST
static const wchar_t *GFX_NAMES[GFX_N] = { L"Shockwave", L"Gold burst", L"Light pillar", L"Confetti cannon", L"Fireworks", L"Dragon", L"Thunder strike", L"Black hole", L"Clown" };
static volatile LONG g_gfx_req;                 /* animation to play (1..N), +0x100 = preview with label */
static volatile LONG g_gfx_sel = 1;
static volatile uintptr_t g_goal_fn;
static volatile LONG g_gfx_mute;
static volatile int g_gfx_enabled = 1;                             /* Settings > Video > Goal Effects (this player's own choice): 1 on, 2 on without sound, 0 off */
#include <mmsystem.h>
static volatile LONG g_ui_dump_req; static void uidump_run(void); static int chardump_run(void); static void goaltab_scan(void);
static uintptr_t gs_pick_victim(const float P[3], int exclude_scorer); static void clown3d_begin(void); static void clown3d_end(void);
static void clown3d_prewarm(void); static void gs_chars_prewarm(void); static char gs_victim_why[96]; static void goalvid_scan(void);
static int gs_pick_fx(void); static void gs_snapshot(void); static wchar_t g_goal_scorer[48]; static volatile uintptr_t g_gi_obj;
static volatile uintptr_t g_ball_loc_addr; static float g_goal_P[3]; static volatile DWORD g_goal_P_tick;   /* where the ball was when the goal was given */
static volatile int g_in_menu;
static int rd(uintptr_t a, void *buf, SIZE_T n);
static void goalfx_goal(void) {                                 /* game thread, when a goal arrives */
    float p[3]; uintptr_t a = g_ball_loc_addr;
    if (a && rd(a, p, 12) && p[0] > -2e6f && p[0] < 2e6f && p[1] > -2e6f && p[1] < 2e6f && p[2] > -2e6f && p[2] < 2e6f) { memcpy(g_goal_P, p, 12); g_goal_P_tick = GetTickCount(); }
    InterlockedExchange(&g_gfx_req, 0x400);                     /* a real goal: the effect is the scorer's (worked out on the effects thread) */
}

/* ---- where things are in the game world (read from outside the game thread; every value is range-checked) */
static struct { uintptr_t ball_cls, pc_cls, ball, pc; int32_t o_root, o_loc, o_pcm, o_cache, o_pawn; int have_cam; } GW = { 0, 0, 0, 0, -1, -1, -1, -1, -1, 0 };
static volatile uintptr_t g_gfx_pc_hint;                                           /* the local controller, as seen by the goal/chat hook */
static uintptr_t gw_balls[16], gw_pcs[16]; static int gw_nb, gw_np; static char gw_why[160];
static int gw_cb(uintptr_t o, uintptr_t cls, void *ctx) {
    (void)ctx; uint32_t fl = 0;
    if (cls != GW.ball_cls && cls != GW.pc_cls) return 0;
    if (!rd(o + 8, &fl, 4) || (fl & 0x10)) return 0;                              /* class default object */
    if (cls == GW.ball_cls) { if (gw_nb < 16) gw_balls[gw_nb++] = o; } else if (gw_np < 16) gw_pcs[gw_np++] = o;
    return 0;
}
static int gw_cls_cb(uintptr_t o, uintptr_t cls, void *ctx) {                      /* learn the two classes by name (first time / new map) */
    (void)ctx; (void)o; static uintptr_t lastc; char nm[64];
    if (cls == lastc) return 0; lastc = cls;
    if (cls == GW.ball_cls || cls == GW.pc_cls || !obj_name(cls, nm, sizeof nm)) return 0;
    if (!strcmp(nm, "SoccerBall_C")) GW.ball_cls = cls; else if (!strcmp(nm, "SP_Controller_C")) GW.pc_cls = cls;
    return GW.ball_cls && GW.pc_cls;
}
static int gw_ok3(const float *v) { for (int i = 0; i < 3; i++) if (!(v[i] > -2e6f && v[i] < 2e6f)) return 0; return 1; }
static uintptr_t gw_last_loc_addr;
static int gw_actor_loc(uintptr_t actor, float out[3]) {
    uintptr_t root = 0; int32_t o1 = -1, o2 = -1;
    if (!prop_off(find_prop(obj_class(actor), "RootComponent"), &o1) || !rdp(actor + o1, &root) || !root) return 0;
    if (!prop_off(find_prop(obj_class(root), "RelativeLocation"), &o2)) return 0;
    gw_last_loc_addr = root + o2;
    return rd(root + o2, out, 12) && gw_ok3(out);
}
typedef struct { float loc[3], rot[3], fov; } GPov;
static int gw_pov_ok(const GPov *p) { return gw_ok3(p->loc) && gw_ok3(p->rot) && p->fov >= 5 && p->fov <= 170 && (fabsf(p->loc[0]) + fabsf(p->loc[1]) + fabsf(p->loc[2]) > 1); }
static uintptr_t gw_pov_addr;                                                      /* where the live camera view sits, once found */
static int gw_cam_find(uintptr_t pc) {                                             /* the camera manager's cached view (location, rotation, FOV) */
    uintptr_t pcm = 0; int32_t o = -1, oc = -1; GPov p;
    if (!prop_off(find_prop(obj_class(pc), "PlayerCameraManager"), &o) || !rdp(pc + o, &pcm) || !pcm) { snprintf(gw_why, sizeof gw_why, "no camera manager (offset %d)", o); return 0; }
    static const char *names[] = { "CameraCachePrivate", "CameraCache", "ViewTarget", "LastFrameCameraCachePrivate" };
    for (int n = 0; n < 4; n++) {
        if (!prop_off(find_prop(obj_class(pcm), names[n]), &oc)) continue;
        for (int d = 0x10; d >= 0; d -= 8)                                         /* a time stamp / target pointer comes first, then the view */
            if (rd(pcm + oc + d, &p, sizeof p) && gw_pov_ok(&p)) { gw_pov_addr = pcm + oc + d; return 1; }
    }
    snprintf(gw_why, sizeof gw_why, "camera view not readable (cache offset %d)", oc);
    return 0;
}
static int gw_cam(GCam *c, int lw) {                                               /* the local player's camera right now */
    GPov p;
    if (!gw_pov_addr || !rd(gw_pov_addr, &p, sizeof p) || !gw_pov_ok(&p)) return 0;
    gcam_set(c, p.loc, p.rot[0], p.rot[1], p.rot[2], p.fov, lw);
    return 1;
}
/* at the start of an animation: find the ball + camera; P = goal spot, gz = pitch height. 0 = not in a match (demo view is used) */
static int gw_begin(float P[3], float *gz) {
    char nm[64];
    {   /* fast path: the ball and controller found a moment ago are still the live ones (no searching, so the effect starts at once) */
        uintptr_t b0 = GW.ball, p0 = GW.pc; float l[3];
        if (b0 && p0 && GW.ball_cls && GW.pc_cls && obj_class(b0) == GW.ball_cls && obj_class(p0) == GW.pc_cls && gw_cam_find(p0) && gw_actor_loc(b0, l)) {
            GPov cp; rd(gw_pov_addr, &cp, sizeof cp); memcpy(P, l, 12); g_ball_loc_addr = gw_last_loc_addr; *gz = P[2] - 11;
            uintptr_t pawn = 0; float pl[3]; int32_t op = -1;
            if (prop_off(find_prop(obj_class(p0), "Pawn"), &op) && rdp(p0 + op, &pawn) && pawn && gw_actor_loc(pawn, pl) && pl[2] - 92 < *gz) *gz = pl[2] - 92;
            if (*gz < P[2] - 400) *gz = P[2] - 400;
            snprintf(gw_why, sizeof gw_why, "camera %.0f %.0f %.0f fov %.0f (known ball)", cp.loc[0], cp.loc[1], cp.loc[2], cp.fov);
            return 1; } }
    gw_why[0] = 0; gw_pov_addr = 0; GW.ball = GW.pc = 0;
    if (GW.ball_cls && !(obj_name(GW.ball_cls, nm, sizeof nm) && !strcmp(nm, "SoccerBall_C"))) GW.ball_cls = 0;
    if (GW.pc_cls && !(obj_name(GW.pc_cls, nm, sizeof nm) && !strcmp(nm, "SP_Controller_C"))) GW.pc_cls = 0;
    if (!GW.ball_cls || !GW.pc_cls) for_each_object(gw_cls_cb, NULL);
    if (!GW.ball_cls || !GW.pc_cls) { snprintf(gw_why, sizeof gw_why, "not in a match (ball class %s, controller class %s)", GW.ball_cls ? "ok" : "missing", GW.pc_cls ? "ok" : "missing"); return 0; }
    gw_nb = gw_np = 0; for_each_object(gw_cb, NULL);
    uintptr_t hint = g_gfx_pc_hint;
    if (hint && obj_class(hint) == GW.pc_cls && gw_cam_find(hint)) GW.pc = hint;
    for (int i = 0; i < gw_np && !GW.pc; i++) if (gw_cam_find(gw_pcs[i])) GW.pc = gw_pcs[i];
    if (!GW.pc) { char w[100]; snprintf(w, sizeof w, "%s", gw_why); snprintf(gw_why, sizeof gw_why, "%d controller(s), %d ball(s): %s", gw_np, gw_nb, w[0] ? w : "no controller"); return 0; }
    GPov cp; rd(gw_pov_addr, &cp, sizeof cp);
    float best = 1e30f;                                                            /* the live ball: the one nearest the camera */
    for (int i = 0; i < gw_nb; i++) { float l[3]; if (!gw_actor_loc(gw_balls[i], l)) continue;
        float d = (l[0] - cp.loc[0]) * (l[0] - cp.loc[0]) + (l[1] - cp.loc[1]) * (l[1] - cp.loc[1]) + (l[2] - cp.loc[2]) * (l[2] - cp.loc[2]);
        if (d < best) { best = d; GW.ball = gw_balls[i]; memcpy(P, l, 12); g_ball_loc_addr = gw_last_loc_addr; } }
    if (!GW.ball) { snprintf(gw_why, sizeof gw_why, "%d ball(s) but no readable position", gw_nb); return 0; }
    *gz = P[2] - 11;                                                              /* ball resting on the grass */
    uintptr_t pawn = 0; float pl[3]; int32_t op = -1;
    if (prop_off(find_prop(obj_class(GW.pc), "Pawn"), &op) && rdp(GW.pc + op, &pawn) && pawn && gw_actor_loc(pawn, pl) && pl[2] - 92 < *gz) *gz = pl[2] - 92;   /* ball in the air: use the player's feet */
    if (*gz < P[2] - 400) *gz = P[2] - 400;
    snprintf(gw_why, sizeof gw_why, "camera %.0f %.0f %.0f fov %.0f", cp.loc[0], cp.loc[1], cp.loc[2], cp.fov);
    return 1;
}

static BOOL CALLBACK gfx_wnd_cb(HWND h, LPARAM l) {
    DWORD pid = 0; wchar_t cls[64]; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetClassNameW(h, cls, 64) && !wcscmp(cls, L"UnrealWindow")) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}
static LRESULT CALLBACK gfx_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return m == WM_NCHITTEST ? HTTRANSPARENT : DefWindowProcW(h, m, w, l); }
static int g_gfx_testkeys;                                    /* F6/F7/F9 only when %LOCALAPPDATA%\\UE4PSO\\pso-fx-test.txt exists (the owner's PC) */
#define CLOWN_FACES 6
static volatile int g_clown_face = 1;                             /* how the Clown's face is made white: way 1 (plain white cloth material) was confirmed in the game */
static LONG gfx_keys(HWND game) {
    if (!g_gfx_testkeys) return 0;
    static int p6, p7; int k6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0, k7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0; LONG r = 0;
    if (game && GetForegroundWindow() == game) {
        if (k6 && !p6) { LONG s = g_gfx_sel % GFX_N + 1; g_gfx_sel = s; r = s | 0x100; }
        else if (k7 && !p7) r = g_gfx_sel | 0x100;
    }
    { static int p9; int k9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0; if (k9 && !p9 && game && GetForegroundWindow() == game) g_ui_dump_req = 1; p9 = k9; }
    p6 = k6; p7 = k7; return r;
}
/* Sound: each effect's sound is played through its own freshly opened output on whatever the default sound device
 * is at that moment, and the output is closed again when the sound has finished. (The simple Windows "play sound"
 * call used before can go quiet for the rest of the session once one of its plays gets stuck.) Only this thread
 * touches it. If opening the output fails the reason is logged and the old call is tried instead. */
static struct { HWAVEOUT wo; WAVEHDR h; DWORD t0, ms; } GSND;
static void gfx_sound_close(void) {
    if (!GSND.wo) return;
    waveOutReset(GSND.wo); waveOutUnprepareHeader(GSND.wo, &GSND.h, sizeof GSND.h); waveOutClose(GSND.wo);
    GSND.wo = NULL; memset(&GSND.h, 0, sizeof GSND.h);
}
static void gfx_sound_idle(void) {                              /* called while nothing is playing an effect */
    if (GSND.wo && ((GSND.h.dwFlags & WHDR_DONE) || GetTickCount() - GSND.t0 > GSND.ms + 1500)) gfx_sound_close();
}
static void gfx_sound(int id) {
    static unsigned char *wav[GFX_N]; static unsigned len[GFX_N]; static int fails; unsigned n = 0;
    if (!wav[id - 1]) { wav[id - 1] = gfx_make_wav(id, &n); len[id - 1] = n; }
    unsigned char *w = wav[id - 1]; n = len[id - 1];
    if (!w || n <= 44) { static int lg; if (lg++ < 3) logf_("goal fx: no sound data for effect %d", id); return; }
    gfx_sound_close();
    WAVEFORMATEX fx; memset(&fx, 0, sizeof fx); memcpy(&fx, w + 20, 16); fx.cbSize = 0;      /* the 16 format bytes of the WAV header */
    unsigned dsz = 0; memcpy(&dsz, w + 40, 4); if (dsz > n - 44) dsz = n - 44;
    MMRESULT r = waveOutOpen(&GSND.wo, WAVE_MAPPER, &fx, 0, 0, CALLBACK_NULL);
    if (r == MMSYSERR_NOERROR) {
        GSND.h.lpData = (LPSTR)(w + 44); GSND.h.dwBufferLength = dsz; GSND.h.dwFlags = 0;
        r = waveOutPrepareHeader(GSND.wo, &GSND.h, sizeof GSND.h);
        if (r == MMSYSERR_NOERROR) r = waveOutWrite(GSND.wo, &GSND.h, sizeof GSND.h);
        if (r == MMSYSERR_NOERROR) { GSND.t0 = GetTickCount(); GSND.ms = fx.nAvgBytesPerSec ? (DWORD)((unsigned long long)dsz * 1000 / fx.nAvgBytesPerSec) : 5000; fails = 0; return; }
        gfx_sound_close();
    }
    GSND.wo = NULL;
    if (fails++ < 8) logf_("goal fx: sound output not available for effect %d (Windows sound error %u), trying the simple way", id, (unsigned)r);
    PlaySoundA((LPCSTR)w, NULL, 0x0004 /*SND_MEMORY*/ | 0x0001 /*SND_ASYNC*/ | 0x0002 /*SND_NODEFAULT*/);
}
static struct { HDC dc; HBITMAP bmp; uint32_t *px, *lo, *tmp; int W, H; HDC tdc; HBITMAP tbmp; uint32_t *tpx; uint16_t *zb; uint8_t *zs; } GF;
static int g_gfx_occl;                                           /* effects are hidden behind players / posts that are nearer (needs the game's depth picture) */
static void gfx_make_sprites(void) {
    static const wchar_t *txt[GFX_NSPR] = { L"HA HA", L"CLOWN" }; enum { SW = 520, SH = 150 };
    BITMAPINFO bi = {0}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = SW; bi.bmiHeader.biHeight = -SH; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(NULL); uint32_t *px = NULL; HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0); if (!bm || !px) { if (dc) DeleteDC(dc); return; }
    HGDIOBJ ob = SelectObject(dc, bm); HFONT f = CreateFontW(-118, 0, 0, 0, 900, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial Black"); HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    for (int k = 0; k < GFX_NSPR; k++) {
        memset(px, 0, SW * SH * 4); RECT r = { 0, 0, SW, SH }; DrawTextW(dc, txt[k], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP); GdiFlush();
        int x0 = SW, x1 = -1, y0 = SH, y1 = -1;
        for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) if ((px[y * SW + x] & 255) > 40) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 < x0) continue;
        int w = x1 - x0 + 1, h = y1 - y0 + 1; uint8_t *m = (uint8_t *)malloc((size_t)w * h); if (!m) continue;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) m[y * w + x] = (uint8_t)(px[(y + y0) * SW + x + x0] & 255);
        g_spr_w[k] = w; g_spr_h[k] = h; g_spr[k] = m;
    }
    SelectObject(dc, of); SelectObject(dc, ob); DeleteObject(f); DeleteObject(bm); DeleteDC(dc);
}
#define GFX_TW 860
#define GFX_TH 44
static void gfx_label(int id, int world) {
    if (!GF.tdc) {
        BITMAPINFO bi = {0}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = GFX_TW; bi.bmiHeader.biHeight = -GFX_TH; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        GF.tdc = CreateCompatibleDC(NULL); GF.tbmp = CreateDIBSection(GF.tdc, &bi, DIB_RGB_COLORS, (void **)&GF.tpx, NULL, 0); if (!GF.tbmp) return;
        SelectObject(GF.tdc, GF.tbmp);
        SelectObject(GF.tdc, CreateFontW(-22, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI"));
        SetBkMode(GF.tdc, TRANSPARENT); SetTextColor(GF.tdc, RGB(255, 255, 255));
    }
    if (!GF.tpx) return;
    memset(GF.tpx, 0, GFX_TW * GFX_TH * 4);
    wchar_t s[160];
    swprintf(s, 160, L"%d / %d   %ls   %ls      F6 next  \x00B7  F7 replay", id, GFX_N, GFX_NAMES[id - 1], world ? L"(at the ball)" : L"(demo view - join a match)");
    RECT r = { 0, 0, GFX_TW, GFX_TH }; DrawTextW(GF.tdc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); GdiFlush();
}
static void gfx_label_blend(float env) {
    if (!GF.tpx || GF.W < GFX_TW + 8 || GF.H < 300) return;
    int ox = (GF.W - GFX_TW) / 2, oy = GF.H * 86 / 100, k = (int)(env * 150);
    if (oy + GFX_TH >= GF.H) return;
    for (int y = 0; y < GFX_TH; y++) { uint32_t *o = GF.px + (size_t)(oy + y) * GF.W + ox; const uint32_t *t = GF.tpx + y * GFX_TW;
        for (int x = 0; x < GFX_TW; x++) {
            uint32_t p = o[x]; int ta = (int)((t[x] & 255) * env), ik = 255 - k, it = 255 - ta;
            int A = (int)(p >> 24) * ik / 255 + k, R = (int)(p >> 16 & 255) * ik / 255, G = (int)(p >> 8 & 255) * ik / 255, B = (int)(p & 255) * ik / 255;
            A = A * it / 255 + ta; R = R * it / 255 + ta; G = G * it / 255 + ta; B = B * it / 255 + ta;
            o[x] = (uint32_t)A << 24 | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
        } }
}
static int gfx_size(int W, int H) {
    if (GF.px && GF.W == W && GF.H == H) return 1;
    if (GF.bmp) { DeleteObject(GF.bmp); GF.bmp = NULL; GF.px = NULL; } free(GF.lo); free(GF.tmp); free(GF.zb); free(GF.zs); GF.lo = GF.tmp = NULL; GF.zb = NULL; GF.zs = NULL;
    if (!GF.dc) GF.dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi = {0}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    GF.bmp = CreateDIBSection(GF.dc, &bi, DIB_RGB_COLORS, (void **)&GF.px, NULL, 0);
    GF.lo = (uint32_t *)malloc((size_t)W * H * 4); GF.tmp = (uint32_t *)malloc((size_t)W * 4);          /* big enough for full-resolution effects too */
    GF.zb = (uint16_t *)malloc((size_t)W * H * 2); GF.zs = (uint8_t *)malloc((size_t)W * H);
    if (!GF.bmp || !GF.lo || !GF.tmp) { GF.W = 0; return 0; }
    SelectObject(GF.dc, GF.bmp); GF.W = W; GF.H = H; return 1;
}
/* plays one animation; returns a new request if one arrived meanwhile */
static LONG gfx_play(HWND wnd, HWND game, LONG req, HANDLE tm) {
    int id = req & 0xFF, preview = (req & 0x100) != 0; RECT cr; POINT p0 = {0, 0};
    if ((req & 0x400) && !g_gfx_enabled) return 0;                   /* switched off in Settings > Video: no effect, no sound, nobody turned into a clown */
    if (req & 0x400) id = gs_pick_fx();
    if (id < 1 || id > GFX_N || IsIconic(game) || !GetClientRect(game, &cr) || !ClientToScreen(game, &p0)) return 0;
    int W = cr.right - cr.left, H = cr.bottom - cr.top;
    if (W < 320 || H < 200 || W > 8192 || H > 4320 || !gfx_size(W, H)) return 0;
    int full = (id == 9 || id == 4) && W <= 4096;                    /* drawn shapes (clown, confetti): full resolution, crisp. Glow effects: half, they are soft anyway */
    int lw = full ? W : (W + 1) / 2, lh = full ? H : (H + 1) / 2; float dur = GFX_DUR[id - 1];
    float P[3] = { 0, 0, 11 }, gz = 0; GCam cam;
    int world = gw_begin(P, &gz);
    if (world && !preview && !(req & 0x200) && g_goal_P_tick && GetTickCount() - g_goal_P_tick < 3000) {      /* the spot where the ball went in, not where it is by now (kick-off) */
        float dz = g_goal_P[2] - P[2]; memcpy(P, g_goal_P, 12); if (gz > P[2] - 11) gz = P[2] - 11; (void)dz; }
    { static int lg; if (lg++ < 40) logf_("goal fx: %s %d: %s - %s (window %dx%d)", preview ? "preview" : "goal", id, world ? "at the ball" : "no world position", gw_why, W, H); }
    if (!world) { float cp[3] = { -1900, 0, 620 }; P[0] = P[1] = 0; P[2] = 11; gz = 0; gcam_set(&cam, cp, -15, 0, 0, 90, lw); }   /* centre-of-screen demo view */
    else if (!gw_cam(&cam, lw)) return 0;
    if (preview) gfx_label(id, world);
    SetWindowLongPtrW(wnd, GWLP_HWNDPARENT, (LONG_PTR)game);
    uintptr_t victim = 0; g_fx_T_ok = 0;                               /* Clown: whose head? The nearest player to where the ball went in (not the scorer) */
    g_fx_3d = 0;
    if (id == 9 && world) { victim = gs_pick_victim(P, (req & 0x400) != 0); if (victim) clown3d_begin(); static int lg; if (lg++ < 20) logf_("goal fx: clown goes on %s", gs_victim_why); }
    if (g_gfx_enabled != 2) gfx_sound(id);
    LARGE_INTEGER f, t0, now; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    float last = 0; int shown = 0, first = 1; LONG next = 0;
    int d3 = d3_usable(); DWORD st = GetTickCount(); static uint32_t lb[GFX_TW * GFX_TH];
    { static int lg; if (lg++ < 6) logf_("goal fx: drawing %s", d3 ? "inside the game (works in fullscreen)" : "in the overlay window"); }
    for (;;) {
        MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        QueryPerformanceCounter(&now); float time = (float)((double)(now.QuadPart - t0.QuadPart) / (double)f.QuadPart);
        if (time >= dur || !IsWindow(game)) break;
        if ((next = InterlockedExchange(&g_gfx_req, 0)) || (next = gfx_keys(game))) break;
        if (world && !gw_cam(&cam, lw)) break;                                  /* left the match */
        if (victim) { float l[3]; if (rd(victim, l, 12) && gw_ok3(l)) { g_fx_T[0] = l[0]; g_fx_T[1] = l[1]; g_fx_T[2] = l[2] + 82; g_fx_T_ok = 1; } else { victim = 0; g_fx_T_ok = 0; } }
        int occ = d3 && world && g_gfx_occl && GF.zb && GF.zs && !(req & 0x200);
        g_zs = GF.zs; g_zb = occ ? GF.zb : NULL;
        gfx_render(id, time, time - last, first, GF.lo, lw, lh, &cam, P, gz); last = time; first = 0;
        g_zb = NULL;
        float cuv[8][2], czv[8]; int cn = 0;                                    /* spots on the pitch around the goal spot whose distance is known: the game's depth is measured there once */
        if (occ && !D3.near_ok) for (int k = 0; k < 8; k++) { float an = k * 0.785f + 0.35f, rr = 220 + 110 * k, pp[3] = { P[0] + cosf(an) * rr, P[1] + sinf(an) * rr, gz }, x, y, z;
            if (gcam_proj(&cam, lw, lh, pp, &x, &y, &z) && x > 4 && x < lw - 4 && y > 4 && y < lh - 4) { cuv[cn][0] = x / lw; cuv[cn][1] = y / lh; czv[cn] = z; cn++; } }
        float le = time < 0.15f ? time / 0.15f : dur - time < 0.25f ? (dur - time) / 0.25f : 1;
        if (d3) {
            if (preview && GF.tpx) { int k = (int)(le * 150); for (int i = 0; i < GFX_TW * GFX_TH; i++) { uint32_t ta = (uint32_t)((GF.tpx[i] & 255) * le); uint32_t A = k + ta * (255 - k) / 255; lb[i] = A << 24 | ta << 16 | ta << 8 | ta; } }
            d3_submit(GF.lo, lw, lh, preview && GF.tpx ? lb : NULL, GFX_TW, GFX_TH, occ ? GF.zb : NULL, cuv, czv, cn);
            if (time > 0.35f && (!D3.draw_tick || (LONG)(D3.draw_tick - st) < 0)) { D3.bad = 1; D3.active = 0; d3 = 0; logf_("goal fx: drawing inside the game didn't work here, using the overlay window from now on"); }
        } else {
            if (full) memcpy(GF.px, GF.lo, (size_t)W * H * 4); else gfx_up(GF.lo, lw, lh, GF.px, W, H, GF.tmp);
            if (preview) gfx_label_blend(le);
            SIZE sz = { W, H }; POINT src = {0, 0}; BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(wnd, NULL, &p0, &sz, GF.dc, &src, 0, &bf, ULW_ALPHA);
            if (!shown) { ShowWindow(wnd, SW_SHOWNOACTIVATE); shown = 1; }
        }
        LARGE_INTEGER due; due.QuadPart = -40000;                              /* ~4 ms pause between frames */
        if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(tm, 20); else Sleep(4);
    }
    if (id == 9) clown3d_end();
    D3.active = 0;
    if (shown) ShowWindow(wnd, SW_HIDE);
    return next;
}
static DWORD WINAPI gfx_thread(LPVOID u) {
    (void)u;
    if (!refl_init()) return 0;
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0}; wc.lpfnWndProc = gfx_proc; wc.hInstance = hi; wc.lpszClassName = L"PSOGoalFx";
    RegisterClassW(&wc);
    HWND wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"PSOGoalFx", L"", WS_POPUP, 0, 0, 16, 16, NULL, NULL, hi, NULL);
    if (!wnd) { logf_("goal fx: window not created (%lu), off", GetLastError()); return 0; }
    gfx_make_sprites();
    HANDLE tm = CreateWaitableTimerExW(NULL, NULL, 0x2 /* high resolution */, TIMER_ALL_ACCESS);
    if (!tm) tm = CreateWaitableTimerW(NULL, FALSE, NULL);
    { wchar_t la[MAX_PATH], p[MAX_PATH + 40]; if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) { swprintf(p, MAX_PATH + 40, L"%ls\\UE4PSO\\pso-fx-test.txt", la); g_gfx_testkeys = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; } }
    g_gfx_occl = g_gfx_testkeys;                                    /* for now only on the owner's PC, until it has been seen working */
    logf_("goal fx: ready (build 21: 9 effects shared between players, in the world, sound, drawn inside the game, Goal Effect tab)%s", g_gfx_testkeys ? " - test keys on: F6 next, F7 replay, F9 list" : "");
    HWND game = NULL; LONG req = 0;
    for (;;) {
        MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!game || !IsWindow(game)) { game = NULL; EnumWindows(gfx_wnd_cb, (LPARAM)&game); }
        { static int tries; static DWORD lastt; DWORD tn = GetTickCount();                 /* hook into the game's own drawing once its window exists */
          if (game && !D3.installed && tries < 4 && tn - lastt > 4000) { lastt = tn; tries++; const char *e = d3_install(); logf_("goal fx: in-game drawing %s%s", e ? "not available: " : "ready", e ? e : ""); if (e && strstr(e, "shader")) tries = 4; } }
        if (InterlockedExchange(&g_ui_dump_req, 0)) { if (g_in_menu) uidump_run(); else chardump_run(); }
        { static DWORD ls; DWORD tn = GetTickCount(); if (tn - ls > 600) { ls = tn; goaltab_scan(); } }
        { static DWORD ls2; DWORD tn = GetTickCount(); if (tn - ls2 > 1500) { ls2 = tn; goalvid_scan(); } }      /* does nothing once the tab is in, or outside the menu */
        { static DWORD lp; DWORD tn = GetTickCount();                                      /* in a match: know the ball before the first goal (looked up once, then only checked) */
          static int misses; if (g_in_menu) misses = 0;
          if (!g_in_menu && tn - lp > (DWORD)(misses < 5 ? 4000 : 30000)) { lp = tn; float l[3]; uintptr_t a = g_ball_loc_addr;
              gs_snapshot();
              { static int nth; if (!(nth++ % 3)) gs_chars_prewarm(); }           /* alone in the match: who else is on the pitch (for the Clown) */
              { static int done; if (g_gfx_testkeys && !done && GW.pc) done = chardump_run(); }      /* owner's PC: note once how the character is built */
              if (a && GW.ball && GW.ball_cls && obj_class(GW.ball) == GW.ball_cls && rd(a, l, 12) && gw_ok3(l)) misses = 0;
              else { float P0[3], gz0; g_ball_loc_addr = 0; if (gw_begin(P0, &gz0)) misses = 0; else misses++; }
              clown3d_prewarm(); } }
        gfx_sound_idle();
        if (!req) req = InterlockedExchange(&g_gfx_req, 0);
        if (!req) req = gfx_keys(game);
        if (!req || !game) { req = 0; Sleep(40); continue; }
        req = gfx_play(wnd, game, req, tm);
    }
}
static void goalfx_start(void) { HANDLE t = CreateThread(NULL, 0, gfx_thread, NULL, 0, NULL); if (t) CloseHandle(t); }
#endif

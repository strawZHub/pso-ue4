/* d3dfx.h - draws the goal animation INSIDE the game's own picture (Direct3D 11), so it also shows in
 * exclusive Fullscreen and in recordings. The game's "present frame" call is extended: right before a
 * frame is shown, our picture (rendered on our own thread) is blended over it. Everything the game had
 * set on the graphics device is put back afterwards. If any step isn't available (not D3D11, no shader
 * compiler...), nothing is hooked and the overlay window is used instead. */
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <d3d11.h>
#include <dxgi.h>
static const GUID D3_IID_Device = { 0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };
static const GUID D3_IID_Tex2D  = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
typedef HRESULT (WINAPI *d3_present_fn)(IDXGISwapChain *, UINT, UINT);
#ifdef D3_TEST
#define D3_BC(v) ((void)0)
#else
static volatile LONG g_bc_rt_;
#define D3_BC(v) (g_bc_rt_ = (v))
#endif
typedef struct { ID3D11Texture2D *tex; ID3D11ShaderResourceView *srv; int w, h; } D3Tex;
static struct {
    CRITICAL_SECTION cs; int installed; d3_present_fn orig;
    volatile LONG active, lbl_on; volatile DWORD usable_tick, draw_tick;
    uint32_t *buf; int bw, bh, dirty;                               /* the animation frame (half resolution, premultiplied BGRA) */
    uint32_t *lbl; int lw, lh, lbl_dirty;                           /* the preview label */
    void *vsb, *psb; SIZE_T vsn, psn;                               /* compiled shaders */
    ID3D11Device *dev; ID3D11VertexShader *vs; ID3D11PixelShader *ps; ID3D11BlendState *bs; ID3D11SamplerState *ss; ID3D11RasterizerState *rs; ID3D11DepthStencilState *ds;
    D3Tex t0, t1; int bad;
    /* depth: the effect hidden behind whatever in the game's picture is nearer (see d3_note_depth / the "psz" shader) */
    uint16_t *zbuf; int zdirty; volatile LONG zwant; D3Tex tz;      /* the effect's own depth per pixel, handed over with each frame */
    void *pszb, *pscb; SIZE_T pszn, pscn; ID3D11PixelShader *psz, *pscal; ID3D11SamplerState *sp; ID3D11Buffer *cb;
    void (STDMETHODCALLTYPE *orig_clear)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
    ID3D11Texture2D *zs_tex, *zs_new; ID3D11ShaderResourceView *zs_srv; int zs_w, zs_h, zs_fmt, zs_new_area, bbw, bbh, zs_fail;
    float nearz; int near_ok, cal_tries, zlog;
    ID3D11Texture2D *cal_rt, *cal_st; ID3D11RenderTargetView *cal_rtv; float cal_uv[8][2], cal_z[8]; int cal_n;
} D3;
#ifdef D3_TEST
static void d3_test_capture(IDXGISwapChain *sc);
#endif
static void d3_tex_free(D3Tex *t) { if (t->srv) ID3D11ShaderResourceView_Release(t->srv); if (t->tex) ID3D11Texture2D_Release(t->tex); memset(t, 0, sizeof *t); }
static void d3_release(void) {
    d3_tex_free(&D3.t0); d3_tex_free(&D3.t1); d3_tex_free(&D3.tz);
    if (D3.psz) ID3D11PixelShader_Release(D3.psz); if (D3.pscal) ID3D11PixelShader_Release(D3.pscal); if (D3.sp) ID3D11SamplerState_Release(D3.sp); if (D3.cb) ID3D11Buffer_Release(D3.cb);
    if (D3.zs_srv) ID3D11ShaderResourceView_Release(D3.zs_srv); if (D3.zs_tex) ID3D11Texture2D_Release(D3.zs_tex);
    if (D3.cal_rtv) ID3D11RenderTargetView_Release(D3.cal_rtv); if (D3.cal_rt) ID3D11Texture2D_Release(D3.cal_rt); if (D3.cal_st) ID3D11Texture2D_Release(D3.cal_st);
    D3.psz = NULL; D3.pscal = NULL; D3.sp = NULL; D3.cb = NULL; D3.zs_srv = NULL; D3.zs_tex = NULL; D3.cal_rtv = NULL; D3.cal_rt = NULL; D3.cal_st = NULL;
    if (D3.vs) ID3D11VertexShader_Release(D3.vs); if (D3.ps) ID3D11PixelShader_Release(D3.ps); if (D3.bs) ID3D11BlendState_Release(D3.bs);
    if (D3.ss) ID3D11SamplerState_Release(D3.ss); if (D3.rs) ID3D11RasterizerState_Release(D3.rs); if (D3.ds) ID3D11DepthStencilState_Release(D3.ds);
    D3.vs = NULL; D3.ps = NULL; D3.bs = NULL; D3.ss = NULL; D3.rs = NULL; D3.ds = NULL; D3.dev = NULL;
}
static int d3_setup(ID3D11Device *dev) {
    d3_release();
    if (FAILED(ID3D11Device_CreateVertexShader(dev, D3.vsb, D3.vsn, NULL, &D3.vs)) || FAILED(ID3D11Device_CreatePixelShader(dev, D3.psb, D3.psn, NULL, &D3.ps))) return 0;
    D3D11_BLEND_DESC bd; memset(&bd, 0, sizeof bd);
    bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_SAMPLER_DESC sd; memset(&sd, 0, sizeof sd); sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxAnisotropy = 1;
    D3D11_RASTERIZER_DESC rd_; memset(&rd_, 0, sizeof rd_); rd_.FillMode = D3D11_FILL_SOLID; rd_.CullMode = D3D11_CULL_NONE; rd_.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC dd; memset(&dd, 0, sizeof dd); dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(ID3D11Device_CreateBlendState(dev, &bd, &D3.bs)) || FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &D3.ss))
        || FAILED(ID3D11Device_CreateRasterizerState(dev, &rd_, &D3.rs)) || FAILED(ID3D11Device_CreateDepthStencilState(dev, &dd, &D3.ds))) { d3_release(); return 0; }
    D3.dev = dev;
    if (D3.pszb && D3.pscb) {
        D3D11_SAMPLER_DESC pd; memset(&pd, 0, sizeof pd); pd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; pd.AddressU = pd.AddressV = pd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; pd.MaxLOD = D3D11_FLOAT32_MAX; pd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        D3D11_BUFFER_DESC cd; memset(&cd, 0, sizeof cd); cd.ByteWidth = 80; cd.Usage = D3D11_USAGE_DYNAMIC; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof td); td.Width = 8; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        D3D11_TEXTURE2D_DESC sd2 = td; sd2.Usage = D3D11_USAGE_STAGING; sd2.BindFlags = 0; sd2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(ID3D11Device_CreatePixelShader(dev, D3.pszb, D3.pszn, NULL, &D3.psz)) || FAILED(ID3D11Device_CreatePixelShader(dev, D3.pscb, D3.pscn, NULL, &D3.pscal))
            || FAILED(ID3D11Device_CreateSamplerState(dev, &pd, &D3.sp)) || FAILED(ID3D11Device_CreateBuffer(dev, &cd, NULL, &D3.cb))
            || FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &D3.cal_rt)) || FAILED(ID3D11Device_CreateTexture2D(dev, &sd2, NULL, &D3.cal_st))
            || FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)D3.cal_rt, NULL, &D3.cal_rtv))) {
            if (D3.psz) ID3D11PixelShader_Release(D3.psz); D3.psz = NULL; }                 /* no depth test then */
    }
    return 1;
}
static int d3_upload16(ID3D11Device *dev, ID3D11DeviceContext *ctx, D3Tex *t, const uint16_t *src, int w, int h) {      /* the effect's depth picture */
    if (!t->tex || t->w != w || t->h != h) {
        d3_tex_free(t);
        D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof td); td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &t->tex))) { t->tex = NULL; return 0; }
        if (FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)t->tex, NULL, &t->srv))) { d3_tex_free(t); return 0; }
        t->w = w; t->h = h;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)t->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return 0;
    for (int y = 0; y < h; y++) memcpy((char *)m.pData + (size_t)y * m.RowPitch, src + (size_t)y * w, (size_t)w * 2);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)t->tex, 0);
    return 1;
}
/* The game's own depth picture. Every frame the game wipes its depth picture before drawing the scene; that call is
 * watched (only while an effect plays) and the picture that is as wide and high as the screen picture is noted. */
static void d3_note_depth(ID3D11DepthStencilView *dsv) {
    ID3D11Resource *res = NULL; ID3D11Texture2D *tex = NULL; D3D11_TEXTURE2D_DESC td;
    ID3D11DepthStencilView_GetResource(dsv, &res); if (!res) return;
    if (SUCCEEDED(ID3D11Resource_QueryInterface(res, &D3_IID_Tex2D, (void **)&tex)) && tex) {
        ID3D11Texture2D_GetDesc(tex, &td);
        int fmt = td.Format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : td.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS
                : td.Format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : td.Format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_R16_UNORM : 0;
        int area = (int)td.Width * (int)td.Height, ok = fmt && td.SampleDesc.Count == 1 && (td.BindFlags & D3D11_BIND_SHADER_RESOURCE) && td.Width >= 320 && td.Height >= 200 && D3.bbw > 0;
        if (ok) { float a1 = (float)td.Width / td.Height, a2 = (float)D3.bbw / D3.bbh; if (a1 < a2 * 0.96f || a1 > a2 * 1.04f) ok = 0; }      /* shadow pictures have other shapes */
        int miss = abs((int)td.Width - D3.bbw) + 1; (void)area;                                /* of those, the one nearest the screen's own size is the scene's */
        if (ok && (!D3.zs_new || miss < D3.zs_new_area)) { if (D3.zs_new) ID3D11Texture2D_Release(D3.zs_new); D3.zs_new = tex; tex = NULL; D3.zs_new_area = miss; D3.zs_fmt = fmt; D3.zs_w = td.Width; D3.zs_h = td.Height; }
        if (tex) ID3D11Texture2D_Release(tex);
    }
    ID3D11Resource_Release(res);
}
static void STDMETHODCALLTYPE d3_clear_dsv(ID3D11DeviceContext *ctx, ID3D11DepthStencilView *dsv, UINT flags, FLOAT depth, UINT8 stencil) {
    if (D3.active && D3.zwant && dsv && (flags & D3D11_CLEAR_DEPTH) && !D3.zs_fail) d3_note_depth(dsv);
    D3.orig_clear(ctx, dsv, flags, depth, stencil);
}
static int d3_upload(ID3D11Device *dev, ID3D11DeviceContext *ctx, D3Tex *t, const uint32_t *src, int w, int h) {
    if (!t->tex || t->w != w || t->h != h) {
        d3_tex_free(t);
        D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof td); td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &t->tex))) { t->tex = NULL; return 0; }
        if (FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)t->tex, NULL, &t->srv))) { d3_tex_free(t); return 0; }
        t->w = w; t->h = h;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)t->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return 0;
    for (int y = 0; y < h; y++) memcpy((char *)m.pData + (size_t)y * m.RowPitch, src + (size_t)y * w, (size_t)w * 4);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)t->tex, 0);
    return 1;
}
static void d3_draw(IDXGISwapChain *sc) {
    ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL; ID3D11Texture2D *bb = NULL; ID3D11RenderTargetView *rtv = NULL;
    if (FAILED(IDXGISwapChain_GetDevice(sc, &D3_IID_Device, (void **)&dev)) || !dev) return;          /* not a D3D11 game */
    D3.usable_tick = GetTickCount();
    if (!D3.active || D3.bad) {
        if (D3.zs_new) { ID3D11Texture2D_Release(D3.zs_new); D3.zs_new = NULL; D3.zs_new_area = 0; }      /* nothing playing: let go of the game's depth picture */
        if (D3.zs_srv) { ID3D11ShaderResourceView_Release(D3.zs_srv); D3.zs_srv = NULL; } if (D3.zs_tex) { ID3D11Texture2D_Release(D3.zs_tex); D3.zs_tex = NULL; }
        ID3D11Device_Release(dev); return; }
    ID3D11Device_GetImmediateContext(dev, &ctx);
    if (!ctx) goto out;
    if (dev != D3.dev && !d3_setup(dev)) { D3.bad = 1; goto out; }
    if (FAILED(IDXGISwapChain_GetBuffer(sc, 0, &D3_IID_Tex2D, (void **)&bb)) || !bb) goto out;
    D3D11_TEXTURE2D_DESC bd; ID3D11Texture2D_GetDesc(bb, &bd);
    if (bd.SampleDesc.Count > 1) goto out;
    D3.bbw = (int)bd.Width; D3.bbh = (int)bd.Height;
    float cuv[8][2], cz[8]; int cn = 0;
    if (FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv)) || !rtv) {
        D3D11_RENDER_TARGET_VIEW_DESC rv; memset(&rv, 0, sizeof rv); rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Format = bd.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : bd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM
                  : bd.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS ? DXGI_FORMAT_R10G10B10A2_UNORM : bd.Format;
        rtv = NULL; if (FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, &rv, &rtv)) || !rtv) goto out;
    }
    if (TryEnterCriticalSection(&D3.cs)) {                          /* take the newest frame from our thread (never wait for it) */
        if (D3.dirty && D3.buf) { d3_upload(dev, ctx, &D3.t0, D3.buf, D3.bw, D3.bh); D3.dirty = 0; }
        if (D3.lbl_dirty && D3.lbl) { d3_upload(dev, ctx, &D3.t1, D3.lbl, D3.lw, D3.lh); D3.lbl_dirty = 0; }
        if (D3.zdirty && D3.zbuf && D3.psz) { d3_upload16(dev, ctx, &D3.tz, D3.zbuf, D3.bw, D3.bh); D3.zdirty = 0; }
        cn = D3.cal_n; memcpy(cuv, D3.cal_uv, sizeof cuv); memcpy(cz, D3.cal_z, sizeof cz);
        LeaveCriticalSection(&D3.cs);
    }
    if (!D3.t0.srv) goto out;
    if (D3.zs_new) {                                                /* the depth picture the game wiped at the start of this frame */
        if (D3.zs_new != D3.zs_tex) {
            if (D3.zs_srv) { ID3D11ShaderResourceView_Release(D3.zs_srv); D3.zs_srv = NULL; } if (D3.zs_tex) ID3D11Texture2D_Release(D3.zs_tex);
            D3.zs_tex = D3.zs_new;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd; memset(&vd, 0, sizeof vd); vd.Format = (DXGI_FORMAT)D3.zs_fmt; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
            if (FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)D3.zs_tex, &vd, &D3.zs_srv)) || !D3.zs_srv) { D3.zs_srv = NULL; D3.zs_fail = 1; logf_("goal fx: the game's depth picture can't be read here (%dx%d, format %d): effects stay in front of everything", D3.zs_w, D3.zs_h, D3.zs_fmt); }
            else if (D3.zlog++ < 6) logf_("goal fx: using the game's depth picture (%dx%d for a %dx%d screen, format %d)", D3.zs_w, D3.zs_h, D3.bbw, D3.bbh, D3.zs_fmt);
        } else ID3D11Texture2D_Release(D3.zs_new);
        D3.zs_new = NULL; D3.zs_new_area = 0;
    }
    int zcan = D3.zwant && D3.psz && D3.zs_srv && !D3.zs_fail && D3.tz.srv && D3.tz.w == D3.t0.w && D3.tz.h == D3.t0.h;
    {   /* remember what the game had set, draw, put it all back */
        ID3D11RenderTargetView *o_rt[8] = {0}; ID3D11DepthStencilView *o_dsv = NULL; ID3D11BlendState *o_bs = NULL; FLOAT o_bf[4]; UINT o_mask = 0;
        ID3D11DepthStencilState *o_ds = NULL; UINT o_ref = 0; ID3D11RasterizerState *o_rs = NULL; D3D11_VIEWPORT o_vp[16]; UINT o_nvp = 16;
        D3D11_PRIMITIVE_TOPOLOGY o_topo; ID3D11InputLayout *o_il = NULL;
        ID3D11VertexShader *o_vs = NULL; ID3D11PixelShader *o_ps = NULL; ID3D11GeometryShader *o_gs = NULL; ID3D11HullShader *o_hs = NULL; ID3D11DomainShader *o_dsh = NULL;
        ID3D11ShaderResourceView *o_srv[3] = {0}; ID3D11SamplerState *o_ss[2] = {0}; ID3D11Buffer *o_cb = NULL;
        ID3D11DeviceContext_OMGetRenderTargets(ctx, 8, o_rt, &o_dsv); ID3D11DeviceContext_OMGetBlendState(ctx, &o_bs, o_bf, &o_mask);
        ID3D11DeviceContext_OMGetDepthStencilState(ctx, &o_ds, &o_ref); ID3D11DeviceContext_RSGetState(ctx, &o_rs); ID3D11DeviceContext_RSGetViewports(ctx, &o_nvp, o_vp);
        ID3D11DeviceContext_IAGetPrimitiveTopology(ctx, &o_topo); ID3D11DeviceContext_IAGetInputLayout(ctx, &o_il);
        ID3D11DeviceContext_VSGetShader(ctx, &o_vs, NULL, NULL); ID3D11DeviceContext_PSGetShader(ctx, &o_ps, NULL, NULL); ID3D11DeviceContext_GSGetShader(ctx, &o_gs, NULL, NULL);
        ID3D11DeviceContext_HSGetShader(ctx, &o_hs, NULL, NULL); ID3D11DeviceContext_DSGetShader(ctx, &o_dsh, NULL, NULL);
        ID3D11DeviceContext_PSGetShaderResources(ctx, 0, 3, o_srv); ID3D11DeviceContext_PSGetSamplers(ctx, 0, 2, o_ss); ID3D11DeviceContext_PSGetConstantBuffers(ctx, 0, 1, &o_cb);

        FLOAT bf[4] = { 0, 0, 0, 0 }; D3D11_VIEWPORT vp = { 0, 0, (FLOAT)bd.Width, (FLOAT)bd.Height, 0, 1 };
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL); ID3D11DeviceContext_OMSetBlendState(ctx, D3.bs, bf, 0xFFFFFFFF);
        ID3D11DeviceContext_OMSetDepthStencilState(ctx, D3.ds, 0); ID3D11DeviceContext_RSSetState(ctx, D3.rs); ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
        ID3D11DeviceContext_VSSetShader(ctx, D3.vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, D3.ps, NULL, 0);
        ID3D11DeviceContext_GSSetShader(ctx, NULL, NULL, 0); ID3D11DeviceContext_HSSetShader(ctx, NULL, NULL, 0); ID3D11DeviceContext_DSSetShader(ctx, NULL, NULL, 0);
        if (zcan) {                                                  /* numbers for the two depth shaders: near distance, softness, where to measure */
            D3D11_MAPPED_SUBRESOURCE cm; float kq[20]; memset(kq, 0, sizeof kq); kq[0] = D3.nearz > 0 ? D3.nearz : 10; kq[1] = 25; kq[2] = 1; kq[3] = 1;
            for (int i = 0; i < 8 && i < cn; i++) { kq[4 + i * 2] = cuv[i][0]; kq[5 + i * 2] = cuv[i][1]; }
            if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)D3.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) { memcpy(cm.pData, kq, sizeof kq); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)D3.cb, 0); } else zcan = 0;
        }
        if (zcan && !D3.near_ok && cn >= 5 && D3.cal_tries < 60) {      /* once: work out the game's near distance by measuring the pitch at spots whose distance is known */
            D3D11_VIEWPORT cv = { 0, 0, 8, 1, 0, 1 }; ID3D11ShaderResourceView *n3[3] = { NULL, NULL, D3.zs_srv }; ID3D11SamplerState *s2[2] = { D3.ss, D3.sp }; D3D11_MAPPED_SUBRESOURCE rm;
            D3.cal_tries++;
            ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &D3.cal_rtv, NULL); ID3D11DeviceContext_OMSetBlendState(ctx, NULL, bf, 0xFFFFFFFF); ID3D11DeviceContext_RSSetViewports(ctx, 1, &cv);
            ID3D11DeviceContext_PSSetShader(ctx, D3.pscal, NULL, 0); ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 3, n3); ID3D11DeviceContext_PSSetSamplers(ctx, 0, 2, s2); ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &D3.cb);
            ID3D11DeviceContext_Draw(ctx, 3, 0);
            ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)D3.cal_st, (ID3D11Resource *)D3.cal_rt);
            if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)D3.cal_st, 0, D3D11_MAP_READ, 0, &rm))) {
                float dv[8], nv[8], srt[8]; int m = 0; memcpy(dv, rm.pData, sizeof dv); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)D3.cal_st, 0);
                for (int i = 0; i < cn && i < 8; i++) if (dv[i] > 1e-6f && dv[i] < 0.9999f && cz[i] > 30) { nv[m] = dv[i] * cz[i]; srt[m] = nv[m]; m++; }
                for (int i = 0; i < m; i++) for (int j = i + 1; j < m; j++) if (srt[j] < srt[i]) { float t = srt[i]; srt[i] = srt[j]; srt[j] = t; }
                float med = m ? srt[m / 2] : 0; int agree = 0; for (int i = 0; i < m; i++) if (nv[i] > med * 0.92f && nv[i] < med * 1.08f) agree++;
                if (m >= 5 && agree >= 5 && med > 0.05f && med < 80) { static const float nice[] = { 0.1f, 0.2f, 0.5f, 1, 2, 2.5f, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 50 }; float use = med;
                    for (unsigned i = 0; i < sizeof nice / sizeof nice[0]; i++) if (med > nice[i] * 0.94f && med < nice[i] * 1.06f) use = nice[i];      /* games use round numbers: take the round one the measurement points to */
                    D3.nearz = use; D3.near_ok = 1; logf_("goal fx: depth test on - the game's near distance is %.2f (measured %.3f, %d of %d spots agree)", use, med, agree, m); }
                else if (D3.cal_tries == 20 || D3.cal_tries == 60) logf_("goal fx: depth test not on yet (try %d): %d spots measured, %d agree, middle value %.3f; raw depth %.5f %.5f %.5f %.5f at distances %.0f %.0f %.0f %.0f", D3.cal_tries, m, agree, med, dv[0], dv[1], dv[2], dv[3], cz[0], cz[1], cz[2], cz[3]);
            }
            ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL); ID3D11DeviceContext_OMSetBlendState(ctx, D3.bs, bf, 0xFFFFFFFF); ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        }
        if (zcan && D3.near_ok) {                                    /* the effect, hidden wherever the game's picture is nearer */
            ID3D11ShaderResourceView *v3[3] = { D3.t0.srv, D3.tz.srv, D3.zs_srv }; ID3D11SamplerState *s2[2] = { D3.ss, D3.sp };
            ID3D11DeviceContext_PSSetShader(ctx, D3.psz, NULL, 0); ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 3, v3); ID3D11DeviceContext_PSSetSamplers(ctx, 0, 2, s2); ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &D3.cb);
            ID3D11DeviceContext_Draw(ctx, 3, 0);
            ID3D11ShaderResourceView *n3[3] = { NULL, NULL, NULL }; ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 3, n3); ID3D11DeviceContext_PSSetShader(ctx, D3.ps, NULL, 0);
        } else {
            ID3D11ShaderResourceView *n3[3] = { D3.t0.srv, NULL, NULL };
            ID3D11DeviceContext_PSSetShader(ctx, D3.ps, NULL, 0); ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 3, n3); ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &D3.ss);
            ID3D11DeviceContext_Draw(ctx, 3, 0);
        }
        if (D3.lbl_on && D3.t1.srv && (int)bd.Width > D3.t1.w + 8 && bd.Height > 300) {                 /* the preview label, as its own small rectangle */
            D3D11_VIEWPORT lv = { (FLOAT)(((int)bd.Width - D3.t1.w) / 2), (FLOAT)(bd.Height * 86 / 100), (FLOAT)D3.t1.w, (FLOAT)D3.t1.h, 0, 1 };
            if (lv.TopLeftY + lv.Height < bd.Height) { ID3D11DeviceContext_RSSetViewports(ctx, 1, &lv); ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &D3.t1.srv); ID3D11DeviceContext_Draw(ctx, 3, 0); }
        }
        D3.draw_tick = GetTickCount();

        ID3D11DeviceContext_OMSetRenderTargets(ctx, 8, o_rt, o_dsv); ID3D11DeviceContext_OMSetBlendState(ctx, o_bs, o_bf, o_mask);
        ID3D11DeviceContext_OMSetDepthStencilState(ctx, o_ds, o_ref); ID3D11DeviceContext_RSSetState(ctx, o_rs); if (o_nvp) ID3D11DeviceContext_RSSetViewports(ctx, o_nvp, o_vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, o_topo); ID3D11DeviceContext_IASetInputLayout(ctx, o_il);
        ID3D11DeviceContext_VSSetShader(ctx, o_vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, o_ps, NULL, 0); ID3D11DeviceContext_GSSetShader(ctx, o_gs, NULL, 0);
        ID3D11DeviceContext_HSSetShader(ctx, o_hs, NULL, 0); ID3D11DeviceContext_DSSetShader(ctx, o_dsh, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 3, o_srv); ID3D11DeviceContext_PSSetSamplers(ctx, 0, 2, o_ss); ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &o_cb);
        for (int i = 0; i < 8; i++) if (o_rt[i]) ID3D11RenderTargetView_Release(o_rt[i]);
        if (o_dsv) ID3D11DepthStencilView_Release(o_dsv); if (o_bs) ID3D11BlendState_Release(o_bs); if (o_ds) ID3D11DepthStencilState_Release(o_ds);
        if (o_rs) ID3D11RasterizerState_Release(o_rs); if (o_il) ID3D11InputLayout_Release(o_il); if (o_vs) ID3D11VertexShader_Release(o_vs);
        if (o_ps) ID3D11PixelShader_Release(o_ps); if (o_gs) ID3D11GeometryShader_Release(o_gs); if (o_hs) ID3D11HullShader_Release(o_hs);
        if (o_dsh) ID3D11DomainShader_Release(o_dsh); for (int i = 0; i < 3; i++) if (o_srv[i]) ID3D11ShaderResourceView_Release(o_srv[i]); for (int i = 0; i < 2; i++) if (o_ss[i]) ID3D11SamplerState_Release(o_ss[i]); if (o_cb) ID3D11Buffer_Release(o_cb);
    }
#ifdef D3_TEST
    d3_test_capture(sc);
#endif
out:
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (bb) ID3D11Texture2D_Release(bb);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(dev);
}
/* Stutter notes: this runs once per drawn frame anyway, so it also times the frames. When, in a match that has been
 * running smoothly, one frame suddenly takes several times longer than usual, a line goes into the log saying how
 * long it took and what the add-on was doing at that moment (looking through objects, playing a goal effect, a step
 * on the game's own thread). At most one line every 2 s and 200 per run. This is how stutters players report can be
 * traced to the add-on, or shown not to come from it. */
static volatile int g_in_menu; static volatile LONG g_bc;
static void d3_frame_watch(void) {
    static LARGE_INTEGER f, prev; static double ema; static DWORD last_log, last_st, match_t0; static int logged, smooth;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!f.QuadPart) { QueryPerformanceFrequency(&f); prev = now; return; }
    double ms = (double)(now.QuadPart - prev.QuadPart) * 1000.0 / (double)f.QuadPart; prev = now;
    if (g_in_menu) match_t0 = 0; else if (!match_t0) match_t0 = GetTickCount();
    if (g_in_menu || ms > 1000) { smooth = 0; ema = 0; return; }             /* menus, loading, the game in the background */
    if (ema <= 0) { ema = ms; return; }
    if (ms > 22 && ms > ema * 3.5 && smooth > 600 && logged < 200) { DWORD t = GetTickCount();
        if (t - last_log > 2000) { last_log = t; logged++;
            logf_("stutter: one frame took %.0f ms (usually %.1f ms, about %.0f fps), %lu s into the match, %lu s after the last one. Add-on at that moment: looking through objects %s (last look ended %lu ms ago), goal effect %s, step on the game thread %ld, on the draw thread %ld",
                  ms, ema, 1000.0 / ema, (unsigned long)((t - match_t0) / 1000), last_st ? (unsigned long)((t - last_st) / 1000) : 0ul, g_scan_busy ? "yes" : "no", (unsigned long)(t - g_scan_end), D3.active ? "playing" : "not playing", g_bc, g_bc_rt_);
            last_st = t; } }
    if (ms < ema * 2) { ema = ema * 0.98 + ms * 0.02; if (smooth < 100000) smooth++; }
}
static HRESULT WINAPI d3_present(IDXGISwapChain *sc, UINT sync, UINT flags) {
    static unsigned n;
    if (!(flags & DXGI_PRESENT_TEST)) d3_frame_watch();
    if (!(flags & DXGI_PRESENT_TEST) && (D3.active || (n++ & 127) == 0)) { D3_BC(31); d3_draw(sc); }   /* idle: one tiny check every 128 frames */
    D3_BC(32); HRESULT hr = D3.orig(sc, sync, flags); D3_BC(0);
    return hr;
}
/* our thread hands over a finished frame */
static void d3_submit(const uint32_t *lo, int lw, int lh, const uint32_t *lbl, int tw, int th, const uint16_t *zb, const float (*cuv)[2], const float *cz, int cn) {
    EnterCriticalSection(&D3.cs);
    if (!D3.buf || D3.bw != lw || D3.bh != lh) { free(D3.buf); free(D3.zbuf); D3.buf = (uint32_t *)malloc((size_t)lw * lh * 4); D3.zbuf = (uint16_t *)malloc((size_t)lw * lh * 2); D3.bw = lw; D3.bh = lh; }
    if (D3.buf) { memcpy(D3.buf, lo, (size_t)lw * lh * 4); D3.dirty = 1; }
    if (zb && D3.zbuf) { memcpy(D3.zbuf, zb, (size_t)lw * lh * 2); D3.zdirty = 1; }
    D3.cal_n = 0; if (zb && cuv && cz) { for (int i = 0; i < cn && i < 8; i++) { D3.cal_uv[i][0] = cuv[i][0]; D3.cal_uv[i][1] = cuv[i][1]; D3.cal_z[i] = cz[i]; } D3.cal_n = cn > 8 ? 8 : cn; }
    D3.zwant = zb != NULL && D3.zbuf != NULL;
    if (lbl) {
        if (!D3.lbl || D3.lw != tw || D3.lh != th) { free(D3.lbl); D3.lbl = (uint32_t *)malloc((size_t)tw * th * 4); D3.lw = tw; D3.lh = th; }
        if (D3.lbl) { memcpy(D3.lbl, lbl, (size_t)tw * th * 4); D3.lbl_dirty = 1; }
    }
    D3.lbl_on = lbl != NULL;
    LeaveCriticalSection(&D3.cs);
    D3.active = 1;
}
static int d3_usable(void) { return D3.installed && !D3.bad && GetTickCount() - D3.usable_tick < 3000; }
static const char *d3_install(void) {                              /* returns NULL when hooked, else why not */
    static const char VS[] = "void main(uint id : SV_VertexID, out float4 p : SV_Position, out float2 uv : TEXCOORD0) { uv = float2((id << 1) & 2, id & 2); p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
    static const char PS[] = "Texture2D t : register(t0); SamplerState s : register(s0); float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return t.Sample(s, uv); }";
    static const char PSZ[] = "Texture2D t : register(t0); Texture2D zt : register(t1); Texture2D sd : register(t2); SamplerState s : register(s0); SamplerState sp : register(s1);"
        " cbuffer C : register(b0) { float4 k; float4 q[4]; };"
        " float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float4 c = t.Sample(s, uv); float ze = zt.Sample(sp, uv).r * 262140.0;"
        " float d = sd.Sample(sp, uv * k.zw).r; float zs = k.x / max(d, 0.0000001) * 1.01 + 10.0; float vis = saturate((zs - ze) / k.y + 1.0); if (ze < 2.0) vis = 1.0; return c * vis; }";
    static const char PSC[] = "Texture2D sd : register(t2); SamplerState sp : register(s1); cbuffer C : register(b0) { float4 k; float4 q[4]; };"
        " float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int i = (int)p.x; float4 v = q[i >> 1]; float2 u = (i & 1) ? v.zw : v.xy;"
        " float d = sd.Sample(sp, u * k.zw).r; return float4(d, 0, 0, 1); }";
    typedef HRESULT (WINAPI *compile_fn)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3D10Blob **, ID3D10Blob **);
    typedef HRESULT (WINAPI *create_fn)(void *, int, HMODULE, UINT, const void *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, void *, ID3D11DeviceContext **);
    if (D3.installed) return NULL;
    HMODULE d3d = GetModuleHandleW(L"d3d11.dll"); if (!d3d) return "the game isn't using Direct3D 11";
    create_fn create = (create_fn)(void *)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain"); if (!create) return "no device function";
    HMODULE comp = LoadLibraryW(L"d3dcompiler_47.dll"); compile_fn compile = comp ? (compile_fn)(void *)GetProcAddress(comp, "D3DCompile") : NULL;
    if (!compile) return "shader compiler (d3dcompiler_47.dll) not found";
    ID3D10Blob *vb = NULL, *pb = NULL;
    if (FAILED(compile(VS, sizeof VS - 1, NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vb, NULL)) || FAILED(compile(PS, sizeof PS - 1, NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &pb, NULL)) || !vb || !pb) return "shader compile failed";
    D3.vsn = ID3D10Blob_GetBufferSize(vb); D3.psn = ID3D10Blob_GetBufferSize(pb); D3.vsb = malloc(D3.vsn); D3.psb = malloc(D3.psn);
    if (!D3.vsb || !D3.psb) return "no memory";
    memcpy(D3.vsb, ID3D10Blob_GetBufferPointer(vb), D3.vsn); memcpy(D3.psb, ID3D10Blob_GetBufferPointer(pb), D3.psn); ID3D10Blob_Release(vb); ID3D10Blob_Release(pb);
    { ID3D10Blob *zb2 = NULL, *cb2 = NULL;                             /* the depth shaders are an extra: without them effects are drawn in front of everything, as before */
      if (SUCCEEDED(compile(PSZ, sizeof PSZ - 1, NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &zb2, NULL)) && zb2 && SUCCEEDED(compile(PSC, sizeof PSC - 1, NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &cb2, NULL)) && cb2) {
          D3.pszn = ID3D10Blob_GetBufferSize(zb2); D3.pscn = ID3D10Blob_GetBufferSize(cb2); D3.pszb = malloc(D3.pszn); D3.pscb = malloc(D3.pscn);
          if (D3.pszb && D3.pscb) { memcpy(D3.pszb, ID3D10Blob_GetBufferPointer(zb2), D3.pszn); memcpy(D3.pscb, ID3D10Blob_GetBufferPointer(cb2), D3.pscn); } else { D3.pszb = NULL; D3.pscb = NULL; } }
      if (zb2) ID3D10Blob_Release(zb2); if (cb2) ID3D10Blob_Release(cb2); }
    /* a throw-away device on a hidden window, only to learn where the "present" entry lives */
    HWND dummy = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 8, 8, NULL, NULL, GetModuleHandleW(NULL), NULL); if (!dummy) return "no helper window";
    DXGI_SWAP_CHAIN_DESC sd; memset(&sd, 0, sizeof sd); sd.BufferCount = 1; sd.BufferDesc.Width = 8; sd.BufferDesc.Height = 8; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = dummy; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    IDXGISwapChain *sc = NULL; ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL;
    HRESULT hr = create(NULL, 1 /* hardware */, NULL, 0, NULL, 0, 7 /* SDK version */, &sd, &sc, &dev, NULL, &ctx);
    if (FAILED(hr) || !sc) { DestroyWindow(dummy); return "helper device not created"; }
    void **vt = *(void ***)sc; DWORD old; const char *err = NULL;
    InitializeCriticalSection(&D3.cs);
    if ((void *)vt[8] == (void *)d3_present) err = NULL;
    else if (VirtualProtect(&vt[8], sizeof(void *), PAGE_READWRITE, &old)) { D3.orig = (d3_present_fn)vt[8]; vt[8] = (void *)d3_present; VirtualProtect(&vt[8], sizeof(void *), old, &old); D3.installed = 1; }
    else err = "present entry is protected";
    if (!err && ctx && D3.pszb && !D3.orig_clear) {                    /* watch the game wiping its depth picture (same kind of entry, on the drawing context) */
        void **slot = (void **)&ctx->lpVtbl->ClearDepthStencilView;
        if ((void *)*slot != (void *)d3_clear_dsv && VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) { D3.orig_clear = (void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8))*slot; *slot = (void *)d3_clear_dsv; VirtualProtect(slot, sizeof(void *), old, &old); }
    }
    IDXGISwapChain_Release(sc); if (ctx) ID3D11DeviceContext_Release(ctx); if (dev) ID3D11Device_Release(dev); DestroyWindow(dummy);
    return err;
}

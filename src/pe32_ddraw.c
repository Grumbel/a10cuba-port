/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_ddraw.h"
#include "pe32_stubs.h"

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"

enum {
    DD_OK = 0, DDERR_INVALIDPARAMS = 0x80070057u, DDERR_UNSUPPORTED = 0x80004001u,
    DDERR_OUTOFMEMORY = 0x8007000eu, E_NOINTERFACE = 0x80004002u,
    DDSD_CAPS = 1, DDSD_HEIGHT = 2, DDSD_WIDTH = 4, DDSD_PITCH = 8,
    DDSD_PIXELFORMAT = 0x1000, DDSD_LPSURFACE = 0x800,
    DDSCAPS_PRIMARYSURFACE = 0x200, DDBLT_COLORFILL = 0x400,
    DDBLTFAST_SRCCOLORKEY = 1, DDSD_SIZE = 108,
    ARENA_BYTES = 16 * 1024 * 1024, MAX_SURFACES = 16, MAX_PALETTES = 8
};

typedef pe32_u32 (PE32_WINAPI *enum_modes_cb)(void *desc, void *context);

struct ddraw {
    pe32_u32 vtbl;
    struct ddraw *state;
    int version;
};

struct palette {
    pe32_u32 vtbl;
    unsigned char entries[256 * 4];
    pe32_u32 flags;
    int used;
};

struct surface {
    pe32_u32 vtbl;
    unsigned char *pixels;
    pe32_u32 width, height, pitch, caps, ck_low, ck_high;
    struct palette *palette;
    int used, primary, color_keyed;
};

static pe32_u32 vtable_dd1[24], vtable_dd2[24], vtable_surface[38],
    vtable_palette[7];
static int vtables_ready;
static struct ddraw dd1_view = {0, &dd1_view, 1}, dd2_view = {0, &dd1_view, 2};
static struct surface surfaces[MAX_SURFACES];
static struct palette palettes[MAX_PALETTES];
static struct surface *primary_surface;
static pe32_u32 mode_width = 640, mode_height = 480, mode_bpp = 8;
static pe32_ddraw_present_fn present_hook;
static unsigned char arena[ARENA_BYTES] __attribute__((aligned(16)));
static pe32_u32 arena_used;

void pe32_ddraw_set_present(pe32_ddraw_present_fn present)
{
    present_hook = present;
}

static void present(void)
{
    struct pe32_ddraw_frame frame;
    if (!present_hook || !primary_surface || !primary_surface->pixels) {
        return;
    }
    frame.pixels = primary_surface->pixels;
    frame.width = primary_surface->width;
    frame.height = primary_surface->height;
    frame.pitch = primary_surface->pitch;
    frame.palette = primary_surface->palette ? primary_surface->palette->entries
                                             : (const unsigned char *)0;
    present_hook(&frame);
}

static pe32_u32 rd(const void *base, pe32_u32 offset)
{
    return *(const pe32_u32 *)((const unsigned char *)base + offset);
}

static void wr(void *base, pe32_u32 offset, pe32_u32 value)
{
    *(pe32_u32 *)((unsigned char *)base + offset) = value;
}

static void clear(void *p, pe32_u32 n)
{
    unsigned char *b = p;
    while (n--) {
        *b++ = 0;
    }
}

static unsigned char *arena_alloc(pe32_u32 bytes)
{
    unsigned char *p;
    bytes = (bytes + 15u) & ~15u;
    if (bytes > ARENA_BYTES - arena_used) {
        return 0;
    }
    p = arena + arena_used;
    arena_used += bytes;
    return p;
}

/* Fill a 108-byte DDSURFACEDESC describing an 8-bit palettized surface. */
static void describe(void *desc, const struct surface *s, int with_pointer)
{
    clear(desc, DDSD_SIZE);
    wr(desc, 0, DDSD_SIZE);
    wr(desc, 4, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH |
                    DDSD_PIXELFORMAT | (with_pointer ? DDSD_LPSURFACE : 0));
    wr(desc, 8, s->height);
    wr(desc, 12, s->width);
    wr(desc, 16, s->pitch);
    wr(desc, 36, with_pointer ? (pe32_u32)(unsigned long)s->pixels : 0);
    wr(desc, 72, 32);          /* DDPIXELFORMAT.dwSize */
    wr(desc, 76, 0x20 | 0x40); /* PALETTEINDEXED8 | RGB */
    wr(desc, 84, 8);           /* bit count */
    wr(desc, 104, s->caps);
}

/* ---- generic unimplemented-method reporters --------------------------- */

#define UNIMPL(fn, label, ...) \
    static pe32_u32 PE32_WINAPI fn(__VA_ARGS__) \
    { \
        static int seen; \
        if (!seen) { \
            seen = 1; \
            pe32_stubs_report("DDRAW.dll", label); \
        } \
        return DDERR_UNSUPPORTED; \
    }

#define A pe32_u32
UNIMPL(dd_compact, "IDirectDraw::Compact", A t)
UNIMPL(dd_create_clipper, "IDirectDraw::CreateClipper", A t, A f, A o, A u)
UNIMPL(dd_duplicate_surface, "IDirectDraw::DuplicateSurface", A t, A s, A o)
UNIMPL(dd_enum_surfaces, "IDirectDraw::EnumSurfaces", A t, A f, A d, A c, A cb)
UNIMPL(dd_get_fourcc, "IDirectDraw::GetFourCCCodes", A t, A n, A c)
UNIMPL(dd_get_gdi_surface, "IDirectDraw::GetGDISurface", A t, A s)
UNIMPL(dd_get_monitor_freq, "IDirectDraw::GetMonitorFrequency", A t, A f)
UNIMPL(dd_get_scan_line, "IDirectDraw::GetScanLine", A t, A l)
UNIMPL(dd_get_vblank, "IDirectDraw::GetVerticalBlankStatus", A t, A b)
UNIMPL(dd_initialize, "IDirectDraw::Initialize", A t, A g)
UNIMPL(dd_get_avail_vid_mem, "IDirectDraw2::GetAvailableVidMem", A t, A c, A tot, A fr)
UNIMPL(s_add_attached, "IDirectDrawSurface::AddAttachedSurface", A t, A s)
UNIMPL(s_add_overlay_dirty, "IDirectDrawSurface::AddOverlayDirtyRect", A t, A r)
UNIMPL(s_blt_batch, "IDirectDrawSurface::BltBatch", A t, A b, A c, A f)
UNIMPL(s_delete_attached, "IDirectDrawSurface::DeleteAttachedSurface", A t, A f, A s)
UNIMPL(s_enum_attached, "IDirectDrawSurface::EnumAttachedSurfaces", A t, A c, A cb)
UNIMPL(s_enum_overlay_z, "IDirectDrawSurface::EnumOverlayZOrders", A t, A f, A c, A cb)
UNIMPL(s_get_attached, "IDirectDrawSurface::GetAttachedSurface", A t, A c, A s)
UNIMPL(s_get_clipper, "IDirectDrawSurface::GetClipper", A t, A c)
UNIMPL(s_get_dc, "IDirectDrawSurface::GetDC", A t, A d)
UNIMPL(s_release_dc, "IDirectDrawSurface::ReleaseDC", A t, A d)
UNIMPL(s_get_overlay_pos, "IDirectDrawSurface::GetOverlayPosition", A t, A x, A y)
UNIMPL(s_initialize, "IDirectDrawSurface::Initialize", A t, A d, A s)
UNIMPL(s_set_clipper, "IDirectDrawSurface::SetClipper", A t, A c)
UNIMPL(s_set_overlay_pos, "IDirectDrawSurface::SetOverlayPosition", A t, A x, A y)
UNIMPL(s_update_overlay, "IDirectDrawSurface::UpdateOverlay", A t, A a, A b, A c, A d, A e)
UNIMPL(s_update_overlay_display, "IDirectDrawSurface::UpdateOverlayDisplay", A t, A f)
UNIMPL(s_update_overlay_z, "IDirectDrawSurface::UpdateOverlayZOrder", A t, A f, A s)
UNIMPL(s_get_dd_interface, "IDirectDrawSurface2::GetDDInterface", A t, A d)
UNIMPL(s_page_lock, "IDirectDrawSurface2::PageLock", A t, A f)
UNIMPL(s_page_unlock, "IDirectDrawSurface2::PageUnlock", A t, A f)
UNIMPL(p_initialize, "IDirectDrawPalette::Initialize", A t, A d, A f, A e)

/* ---- reference counting (objects are static; nothing is freed) ---------- */

static pe32_u32 PE32_WINAPI com_add_ref(pe32_u32 self)
{
    (void)self;
    return 2;
}

static pe32_u32 PE32_WINAPI com_release(pe32_u32 self)
{
    (void)self;
    return 1;
}

/* ---- IDirectDraw ------------------------------------------------------- */

static pe32_u32 PE32_WINAPI dd_query_interface(pe32_u32 self, const void *iid,
                                               pe32_u32 *out)
{
    (void)self;
    if (!iid || !out) {
        return DDERR_INVALIDPARAMS;
    }
    switch (rd(iid, 0)) {
    case 0x6c14db80u: *out = (pe32_u32)(unsigned long)&dd1_view; return DD_OK;
    case 0xb3a6f3e0u: *out = (pe32_u32)(unsigned long)&dd2_view; return DD_OK;
    default:
        *out = 0;
        pe32_stubs_report("DDRAW.dll", "IDirectDraw::QueryInterface(unknown)");
        return E_NOINTERFACE;
    }
}

static struct palette *palette_new(void)
{
    int i;
    for (i = 0; i < (int)MAX_PALETTES; i++) {
        if (!palettes[i].used) {
            clear(&palettes[i], sizeof palettes[i]);
            palettes[i].used = 1;
            palettes[i].vtbl = (pe32_u32)(unsigned long)vtable_palette;
            return &palettes[i];
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI dd_create_palette(pe32_u32 self, pe32_u32 flags,
                                              const unsigned char *entries,
                                              pe32_u32 *out, pe32_u32 outer)
{
    struct palette *p;
    pe32_u32 i;
    (void)self; (void)outer;
    if (!out) {
        return DDERR_INVALIDPARAMS;
    }
    p = palette_new();
    if (!p) {
        return DDERR_OUTOFMEMORY;
    }
    p->flags = flags;
    if (entries) {
        for (i = 0; i < 256 * 4; i++) {
            p->entries[i] = entries[i];
        }
    }
    *out = (pe32_u32)(unsigned long)p;
    return DD_OK;
}

static struct surface *surface_new(pe32_u32 width, pe32_u32 height, pe32_u32 caps)
{
    int i;
    for (i = 0; i < (int)MAX_SURFACES; i++) {
        if (!surfaces[i].used) {
            struct surface *s = &surfaces[i];
            clear(s, sizeof *s);
            s->pitch = (width + 3u) & ~3u;
            s->pixels = arena_alloc(s->pitch * height);
            if (!s->pixels) {
                return 0;
            }
            clear(s->pixels, s->pitch * height);
            s->used = 1;
            s->vtbl = (pe32_u32)(unsigned long)vtable_surface;
            s->width = width;
            s->height = height;
            s->caps = caps;
            return s;
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI dd_create_surface(pe32_u32 self, const void *desc,
                                              pe32_u32 *out, pe32_u32 outer)
{
    pe32_u32 flags, caps = 0, w, h;
    struct surface *s;
    (void)self; (void)outer;
    if (!desc || !out) {
        return DDERR_INVALIDPARAMS;
    }
    flags = rd(desc, 4);
    if (flags & DDSD_CAPS) {
        caps = rd(desc, 104);
    }
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        w = mode_width;
        h = mode_height;
    } else {
        w = (flags & DDSD_WIDTH) ? rd(desc, 12) : mode_width;
        h = (flags & DDSD_HEIGHT) ? rd(desc, 8) : mode_height;
    }
    if (!w || !h || w > 4096 || h > 4096) {
        return DDERR_INVALIDPARAMS;
    }
    s = surface_new(w, h, caps);
    if (!s) {
        return DDERR_OUTOFMEMORY;
    }
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        s->primary = 1;
        primary_surface = s;
    }
    *out = (pe32_u32)(unsigned long)s;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_enum_display_modes(pe32_u32 self, pe32_u32 flags,
                                                  const void *filter,
                                                  void *context,
                                                  pe32_u32 callback)
{
    static const pe32_u32 modes[][2] = {
        {320, 200}, {320, 240}, {640, 400}, {640, 480}, {800, 600}, {1024, 768}};
    unsigned char desc[DDSD_SIZE];
    struct surface fake;
    unsigned i;
    (void)self; (void)flags; (void)filter;
    if (!callback) {
        return DDERR_INVALIDPARAMS;
    }
    for (i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        clear(&fake, sizeof fake);
        fake.width = modes[i][0];
        fake.height = modes[i][1];
        fake.pitch = fake.width;
        describe(desc, &fake, 0);
#if defined(__i386__)
        if (!((enum_modes_cb)(unsigned long)callback)(desc, context)) {
            break;
        }
#else
        (void)context;
#endif
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_flip_to_gdi(pe32_u32 self)
{
    (void)self;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_get_caps(pe32_u32 self, unsigned char *drv,
                                        unsigned char *hel)
{
    unsigned char *tables[2];
    int i;
    (void)self;
    tables[0] = drv;
    tables[1] = hel;
    for (i = 0; i < 2; i++) {
        if (tables[i]) {
            pe32_u32 size = rd(tables[i], 0);
            if (size > 0 && size <= 1024) {
                clear(tables[i], size);
                wr(tables[i], 0, size);
                wr(tables[i], 4, 0x40 | 0x400000); /* BLT | PALETTE */
                wr(tables[i], 64, 4u << 20);       /* video memory total */
                wr(tables[i], 68, 4u << 20);       /* video memory free */
            }
        }
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_get_display_mode(pe32_u32 self, void *desc)
{
    struct surface fake;
    (void)self;
    if (!desc) {
        return DDERR_INVALIDPARAMS;
    }
    clear(&fake, sizeof fake);
    fake.width = mode_width;
    fake.height = mode_height;
    fake.pitch = mode_width;
    describe(desc, &fake, 0);
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_restore_display_mode(pe32_u32 self)
{
    (void)self;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd_set_cooperative_level(pe32_u32 self, pe32_u32 hwnd,
                                                     pe32_u32 flags)
{
    (void)self; (void)hwnd; (void)flags;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd1_set_display_mode(pe32_u32 self, pe32_u32 w,
                                                 pe32_u32 h, pe32_u32 bpp)
{
    (void)self;
    mode_width = w;
    mode_height = h;
    mode_bpp = bpp;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI dd2_set_display_mode(pe32_u32 self, pe32_u32 w,
                                                 pe32_u32 h, pe32_u32 bpp,
                                                 pe32_u32 refresh, pe32_u32 flags)
{
    (void)refresh; (void)flags;
    return dd1_set_display_mode(self, w, h, bpp);
}

static pe32_u32 PE32_WINAPI dd_wait_vblank(pe32_u32 self, pe32_u32 flags,
                                           pe32_u32 event)
{
    (void)self; (void)flags; (void)event;
    return DD_OK;
}

/* ---- IDirectDrawSurface ------------------------------------------------ */

static pe32_u32 PE32_WINAPI s_query_interface(pe32_u32 self, const void *iid,
                                              pe32_u32 *out)
{
    if (!iid || !out) {
        return DDERR_INVALIDPARAMS;
    }
    if (rd(iid, 0) == 0x6c14db81u || rd(iid, 0) == 0x57805885u) {
        *out = self;
        return DD_OK;
    }
    *out = 0;
    pe32_stubs_report("DDRAW.dll", "IDirectDrawSurface::QueryInterface(unknown)");
    return E_NOINTERFACE;
}

/* Clip a rectangle (l,t,r,b or null for all) to a surface. */
static int get_rect(const struct surface *s, const int *r, int *out)
{
    out[0] = r ? r[0] : 0;
    out[1] = r ? r[1] : 0;
    out[2] = r ? r[2] : (int)s->width;
    out[3] = r ? r[3] : (int)s->height;
    if (out[0] < 0) out[0] = 0;
    if (out[1] < 0) out[1] = 0;
    if (out[2] > (int)s->width) out[2] = (int)s->width;
    if (out[3] > (int)s->height) out[3] = (int)s->height;
    return out[0] < out[2] && out[1] < out[3];
}

static void copy_rect(struct surface *dst, int dx, int dy,
                      const struct surface *src, const int *sr, int keyed)
{
    int x, y, r[4];
    if (!get_rect(src, sr, r)) {
        return;
    }
    for (y = r[1]; y < r[3]; y++) {
        int ty = dy + (y - r[1]);
        if (ty < 0 || ty >= (int)dst->height) {
            continue;
        }
        for (x = r[0]; x < r[2]; x++) {
            int tx = dx + (x - r[0]);
            unsigned char v = src->pixels[y * src->pitch + x];
            if (tx < 0 || tx >= (int)dst->width) {
                continue;
            }
            if (keyed && v >= src->ck_low && v <= src->ck_high) {
                continue;
            }
            dst->pixels[ty * dst->pitch + tx] = v;
        }
    }
}

static pe32_u32 PE32_WINAPI s_blt(pe32_u32 self, const int *dr, pe32_u32 src,
                                  const int *sr, pe32_u32 flags, const void *fx)
{
    struct surface *d = (struct surface *)(unsigned long)self;
    struct surface *s = (struct surface *)(unsigned long)src;
    int r[4], x, y;
    if (flags & DDBLT_COLORFILL) {
        if (!fx || !get_rect(d, dr, r)) {
            return fx ? DD_OK : DDERR_INVALIDPARAMS;
        }
        for (y = r[1]; y < r[3]; y++) {
            for (x = r[0]; x < r[2]; x++) {
                d->pixels[y * d->pitch + x] = (unsigned char)rd(fx, 80);
            }
        }
    } else if (s) {
        int sw = sr ? sr[2] - sr[0] : (int)s->width;
        int sh = sr ? sr[3] - sr[1] : (int)s->height;
        int dw = dr ? dr[2] - dr[0] : (int)d->width;
        int dh = dr ? dr[3] - dr[1] : (int)d->height;
        int sx0 = sr ? sr[0] : 0, sy0 = sr ? sr[1] : 0;
        int dx0 = dr ? dr[0] : 0, dy0 = dr ? dr[1] : 0;
        if (sw == dw && sh == dh) {
            copy_rect(d, dx0, dy0, s, sr, (flags & 0x8000) != 0);
        } else if (sw > 0 && sh > 0 && dw > 0 && dh > 0) {
            for (y = 0; y < dh; y++) {   /* nearest-neighbor stretch */
                for (x = 0; x < dw; x++) {
                    int tx = dx0 + x, ty = dy0 + y;
                    unsigned char v = s->pixels[(sy0 + y * sh / dh) * s->pitch +
                                                sx0 + x * sw / dw];
                    if (tx >= 0 && ty >= 0 && tx < (int)d->width &&
                        ty < (int)d->height) {
                        d->pixels[ty * d->pitch + tx] = v;
                    }
                }
            }
        }
    }
    if (d->primary) {
        present();
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_blt_fast(pe32_u32 self, pe32_u32 x, pe32_u32 y,
                                       pe32_u32 src, const int *sr, pe32_u32 flags)
{
    struct surface *d = (struct surface *)(unsigned long)self;
    struct surface *s = (struct surface *)(unsigned long)src;
    if (!s) {
        return DDERR_INVALIDPARAMS;
    }
    copy_rect(d, (int)x, (int)y, s, sr, (flags & DDBLTFAST_SRCCOLORKEY) != 0);
    if (d->primary) {
        present();
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_flip(pe32_u32 self, pe32_u32 target, pe32_u32 flags)
{
    (void)self; (void)target; (void)flags;
    present();
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_blt_status(pe32_u32 self, pe32_u32 flags)
{
    (void)self; (void)flags;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_caps(pe32_u32 self, pe32_u32 *caps)
{
    if (!caps) {
        return DDERR_INVALIDPARAMS;
    }
    *caps = ((struct surface *)(unsigned long)self)->caps;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_color_key(pe32_u32 self, pe32_u32 flags,
                                            pe32_u32 *key)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    (void)flags;
    if (!key) {
        return DDERR_INVALIDPARAMS;
    }
    key[0] = s->ck_low;
    key[1] = s->ck_high;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_flip_status(pe32_u32 self, pe32_u32 flags)
{
    (void)self; (void)flags;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_palette(pe32_u32 self, pe32_u32 *out)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    if (!out || !s->palette) {
        return 0x887600e6u; /* DDERR_NOPALETTEATTACHED */
    }
    *out = (pe32_u32)(unsigned long)s->palette;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_pixel_format(pe32_u32 self, void *pf)
{
    unsigned char desc[DDSD_SIZE];
    int i;
    if (!pf) {
        return DDERR_INVALIDPARAMS;
    }
    describe(desc, (struct surface *)(unsigned long)self, 0);
    for (i = 0; i < 32; i++) {
        ((unsigned char *)pf)[i] = desc[72 + i];
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_get_surface_desc(pe32_u32 self, void *desc)
{
    if (!desc) {
        return DDERR_INVALIDPARAMS;
    }
    describe(desc, (struct surface *)(unsigned long)self, 0);
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_is_lost(pe32_u32 self)
{
    (void)self;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_lock(pe32_u32 self, const int *rect, void *desc,
                                   pe32_u32 flags, pe32_u32 event)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    int r[4];
    (void)flags; (void)event;
    if (!desc) {
        return DDERR_INVALIDPARAMS;
    }
    describe(desc, s, 1);
    if (rect && get_rect(s, rect, r)) {
        wr(desc, 36, (pe32_u32)(unsigned long)(s->pixels + r[1] * s->pitch + r[0]));
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_restore(pe32_u32 self)
{
    (void)self;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_set_color_key(pe32_u32 self, pe32_u32 flags,
                                            const pe32_u32 *key)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    (void)flags;
    if (key) {
        s->ck_low = key[0];
        s->ck_high = key[1];
        s->color_keyed = 1;
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_set_palette(pe32_u32 self, pe32_u32 palette)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    s->palette = (struct palette *)(unsigned long)palette;
    if (s->primary) {
        present();
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI s_unlock(pe32_u32 self, pe32_u32 ptr)
{
    struct surface *s = (struct surface *)(unsigned long)self;
    (void)ptr;
    if (s->primary) {
        present();
    }
    return DD_OK;
}

/* ---- IDirectDrawPalette ------------------------------------------------ */

static pe32_u32 PE32_WINAPI p_get_caps(pe32_u32 self, pe32_u32 *caps)
{
    if (!caps) {
        return DDERR_INVALIDPARAMS;
    }
    *caps = ((struct palette *)(unsigned long)self)->flags;
    return DD_OK;
}

static pe32_u32 PE32_WINAPI p_get_entries(pe32_u32 self, pe32_u32 flags,
                                          pe32_u32 start, pe32_u32 count,
                                          unsigned char *out)
{
    struct palette *p = (struct palette *)(unsigned long)self;
    pe32_u32 i;
    (void)flags;
    if (!out || start > 256 || count > 256 - start) {
        return DDERR_INVALIDPARAMS;
    }
    for (i = 0; i < count * 4; i++) {
        out[i] = p->entries[start * 4 + i];
    }
    return DD_OK;
}

static pe32_u32 PE32_WINAPI p_set_entries(pe32_u32 self, pe32_u32 flags,
                                          pe32_u32 start, pe32_u32 count,
                                          const unsigned char *in)
{
    struct palette *p = (struct palette *)(unsigned long)self;
    pe32_u32 i;
    (void)flags;
    if (!in || start > 256 || count > 256 - start) {
        return DDERR_INVALIDPARAMS;
    }
    for (i = 0; i < count * 4; i++) {
        p->entries[start * 4 + i] = in[i];
    }
    return DD_OK;
}

/* ---- setup -------------------------------------------------------------- */

#define V(table, index, fn) (table)[index] = (pe32_u32)(unsigned long)&(fn)

static void init_vtables(void)
{
    int i;
    if (vtables_ready) {
        return;
    }
    vtables_ready = 1;
    for (i = 0; i < 2; i++) {
        pe32_u32 *t = i ? vtable_dd2 : vtable_dd1;
        V(t, 0, dd_query_interface); V(t, 1, com_add_ref); V(t, 2, com_release);
        V(t, 3, dd_compact); V(t, 4, dd_create_clipper);
        V(t, 5, dd_create_palette); V(t, 6, dd_create_surface);
        V(t, 7, dd_duplicate_surface); V(t, 8, dd_enum_display_modes);
        V(t, 9, dd_enum_surfaces); V(t, 10, dd_flip_to_gdi);
        V(t, 11, dd_get_caps); V(t, 12, dd_get_display_mode);
        V(t, 13, dd_get_fourcc); V(t, 14, dd_get_gdi_surface);
        V(t, 15, dd_get_monitor_freq); V(t, 16, dd_get_scan_line);
        V(t, 17, dd_get_vblank); V(t, 18, dd_initialize);
        V(t, 19, dd_restore_display_mode); V(t, 20, dd_set_cooperative_level);
        V(t, 22, dd_wait_vblank);
    }
    V(vtable_dd1, 21, dd1_set_display_mode);
    V(vtable_dd2, 21, dd2_set_display_mode);
    V(vtable_dd2, 23, dd_get_avail_vid_mem);
    dd1_view.vtbl = (pe32_u32)(unsigned long)vtable_dd1;
    dd2_view.vtbl = (pe32_u32)(unsigned long)vtable_dd2;
    {
        pe32_u32 *t = vtable_surface;
        V(t, 0, s_query_interface); V(t, 1, com_add_ref); V(t, 2, com_release);
        V(t, 3, s_add_attached); V(t, 4, s_add_overlay_dirty); V(t, 5, s_blt);
        V(t, 6, s_blt_batch); V(t, 7, s_blt_fast); V(t, 8, s_delete_attached);
        V(t, 9, s_enum_attached); V(t, 10, s_enum_overlay_z); V(t, 11, s_flip);
        V(t, 12, s_get_attached); V(t, 13, s_get_blt_status);
        V(t, 14, s_get_caps); V(t, 15, s_get_clipper);
        V(t, 16, s_get_color_key); V(t, 17, s_get_dc);
        V(t, 18, s_get_flip_status); V(t, 19, s_get_overlay_pos);
        V(t, 20, s_get_palette); V(t, 21, s_get_pixel_format);
        V(t, 22, s_get_surface_desc); V(t, 23, s_initialize);
        V(t, 24, s_is_lost); V(t, 25, s_lock); V(t, 26, s_release_dc);
        V(t, 27, s_restore); V(t, 28, s_set_clipper);
        V(t, 29, s_set_color_key); V(t, 30, s_set_overlay_pos);
        V(t, 31, s_set_palette); V(t, 32, s_unlock);
        V(t, 33, s_update_overlay); V(t, 34, s_update_overlay_display);
        V(t, 35, s_update_overlay_z); V(t, 36, s_get_dd_interface);
        V(t, 37, s_page_lock);
    }
    {
        pe32_u32 *t = vtable_palette;
        V(t, 0, s_query_interface); V(t, 1, com_add_ref); V(t, 2, com_release);
        V(t, 3, p_get_caps); V(t, 4, p_get_entries); V(t, 5, p_initialize);
        V(t, 6, p_set_entries);
    }
}

pe32_u32 PE32_WINAPI pe32_ddraw_create(const void *guid, pe32_u32 *out,
                                       pe32_u32 outer)
{
    (void)guid; (void)outer;
    if (!out) {
        return DDERR_INVALIDPARAMS;
    }
    init_vtables();
    *out = (pe32_u32)(unsigned long)&dd1_view;
    return DD_OK;
}

#if defined(__i386__)
static int names_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
#endif

enum pe32_link_status pe32_ddraw_resolve(const char *name, pe32_u32 *address)
{
#if defined(__i386__)
    if (names_equal(name, "DirectDrawCreate")) {
        *address = (pe32_u32)(unsigned long)&pe32_ddraw_create;
        return PE32_LINK_OK;
    }
#else
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}

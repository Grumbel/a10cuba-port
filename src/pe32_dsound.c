/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_dsound.h"
#include "pe32_stubs.h"

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"

enum {
    DS_OK = 0, DSERR_INVALIDPARAM = 0x80070057u, DSERR_OUTOFMEMORY = 0x8007000eu,
    DSERR_UNSUPPORTED = 0x80004001u, E_NOINTERFACE = 0x80004002u,
    DSBCAPS_PRIMARYBUFFER = 1, ARENA_BYTES = 32 * 1024 * 1024, MAX_BUFFERS = 128
};

struct dsound {
    pe32_u32 vtbl;
};

struct buffer {
    pe32_u32 vtbl;
    unsigned char *data;
    pe32_u32 bytes, flags, position, volume, frequency, playing, looping, used;
    pe32_u32 frame, frac;
    int pan;
    unsigned char format[18];
};

enum { MIX_RATE = 44100 };

static pe32_u32 block_align(const struct buffer *b);
static pe32_u32 vtable_ds[11], vtable_buffer[21];
static struct dsound ds_object;
static struct buffer buffers[MAX_BUFFERS];
static unsigned char arena[ARENA_BYTES] __attribute__((aligned(16)));
static pe32_u32 arena_used;
static int ready;

static pe32_u32 rd(const void *base, pe32_u32 offset)
{
    return *(const pe32_u32 *)((const unsigned char *)base + offset);
}

static void clear(void *p, pe32_u32 n)
{
    unsigned char *b = p;
    while (n--) {
        *b++ = 0;
    }
}

#define A pe32_u32
#define UNIMPL(fn, label, ...) \
    static pe32_u32 PE32_WINAPI fn(__VA_ARGS__) \
    { \
        pe32_stubs_report("DSOUND.dll", label); \
        return DSERR_UNSUPPORTED; \
    }

static pe32_u32 PE32_WINAPI com_add_ref(A self) { return 2; }
static pe32_u32 PE32_WINAPI com_release(A self) { return 1; }

static pe32_u32 PE32_WINAPI query_interface(A self, const void *iid, A *out)
{
    if (!out) {
        return DSERR_INVALIDPARAM;
    }
    *out = self;
    return DS_OK;
}

static struct buffer *buffer_new(pe32_u32 bytes, pe32_u32 flags)
{
    int i;
    for (i = 0; i < (int)MAX_BUFFERS; i++) {
        if (!buffers[i].used) {
            struct buffer *b = &buffers[i];
            clear(b, sizeof *b);
            bytes = (bytes + 15u) & ~15u;
            if (bytes > ARENA_BYTES - arena_used) {
                return 0;
            }
            b->data = arena + arena_used;
            arena_used += bytes;
            b->bytes = bytes;
            b->flags = flags;
            b->used = 1;
            b->frequency = 22050;
            b->vtbl = (pe32_u32)(unsigned long)vtable_buffer;
            return b;
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI ds_create_sound_buffer(A self, const void *desc,
                                                   A *out, A outer)
{
    pe32_u32 flags, bytes;
    struct buffer *b;
    if (!desc || !out) {
        return DSERR_INVALIDPARAM;
    }
    flags = rd(desc, 4);
    bytes = rd(desc, 8);
    b = buffer_new(bytes, flags);
    if (!b) {
        return DSERR_OUTOFMEMORY;
    }
    if (rd(desc, 16)) {
        const unsigned char *f = (const unsigned char *)(unsigned long)rd(desc, 16);
        int i;
        for (i = 0; i < 16; i++) {
            b->format[i] = f[i];
        }
        b->frequency = rd(f, 4);
    }
    *out = (A)(unsigned long)b;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_get_caps(A self, unsigned char *caps)
{
    pe32_u32 size;
    if (!caps) {
        return DSERR_INVALIDPARAM;
    }
    size = rd(caps, 0);
    if (size > 0 && size <= 256) {
        clear(caps, size);
        *(pe32_u32 *)caps = size;
        *(pe32_u32 *)(caps + 4) = 0x1 | 0x2 | 0x10 | 0x20; /* modes */
        *(pe32_u32 *)(caps + 8) = 8000;                    /* min rate */
        *(pe32_u32 *)(caps + 12) = 48000;                  /* max rate */
    }
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_duplicate(A self, A src, A *out)
{
    struct buffer *s = (struct buffer *)(unsigned long)src, *b;
    pe32_u32 i;
    if (!s || !out) {
        return DSERR_INVALIDPARAM;
    }
    b = buffer_new(s->bytes, s->flags);
    if (!b) {
        return DSERR_OUTOFMEMORY;
    }
    for (i = 0; i < s->bytes; i++) {
        b->data[i] = s->data[i];
    }
    b->frequency = s->frequency;
    for (i = 0; i < 18; i++) {
        b->format[i] = s->format[i];
    }
    *out = (A)(unsigned long)b;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_set_cooperative_level(A self, A hwnd, A level)
{
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_compact(A self) { return DS_OK; }

static pe32_u32 PE32_WINAPI ds_get_speaker_config(A self, A *config)
{
    if (config) {
        *config = 4; /* DSSPEAKER_STEREO */
    }
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_set_speaker_config(A self, A config)
{
    return DS_OK;
}

static pe32_u32 PE32_WINAPI ds_initialize(A self, A guid) { return DS_OK; }

/* Buffers */

/* ---- Software mixer: stereo S16 at MIX_RATE, driven by the host clock. ---- */

static pe32_u32 block_align(const struct buffer *b)
{
    pe32_u32 ch = b->format[2] | (pe32_u32)b->format[3] << 8;
    pe32_u32 bits = b->format[14] | (pe32_u32)b->format[15] << 8;
    ch = ch ? ch : 1;
    bits = bits ? bits : 16;
    return ch * (bits / 8 ? bits / 8 : 1);
}

/* 2^(-k/8) in 16.16, then shifted by whole 6.02 dB steps. */
static pe32_u32 gain_from_millibel(int mb)
{
    static const pe32_u32 frac[8] = {65536, 60097, 55109, 50535,
                                     46341, 42494, 38968, 35734};
    pe32_u32 steps, k;
    if (mb >= 0) {
        return 65536;
    }
    if (mb <= -10000) {
        return 0;
    }
    steps = (pe32_u32)(-mb) / 602;
    k = ((pe32_u32)(-mb) % 602) * 8 / 602;
    return frac[k] >> steps;
}

static int sample_at(const struct buffer *b, pe32_u32 frame, pe32_u32 channel)
{
    pe32_u32 ch = b->format[2] | (pe32_u32)b->format[3] << 8;
    pe32_u32 bits = b->format[14] | (pe32_u32)b->format[15] << 8;
    const unsigned char *p;
    ch = ch ? ch : 1;
    bits = bits ? bits : 16;
    if (channel >= ch) {
        channel = ch - 1;
    }
    if (bits == 8) {
        p = b->data + frame * ch + channel;
        return ((int)*p - 128) << 8;
    }
    p = b->data + (frame * ch + channel) * 2;
    return (short)(p[0] | p[1] << 8);
}

unsigned pe32_dsound_mix(short *out, unsigned frames)
{
    unsigned i, n;
    for (i = 0; i < frames * 2; i++) {
        out[i] = 0;
    }
    for (n = 0; n < MAX_BUFFERS; n++) {
        struct buffer *b = &buffers[n];
        pe32_u32 align, total, step, left, right;
        if (!b->used || !b->playing || (b->flags & DSBCAPS_PRIMARYBUFFER)) {
            continue;
        }
        align = block_align(b);
        total = b->bytes / align;
        if (!total) {
            continue;
        }
        step = b->frequency * 4096u / MIX_RATE;
        left = gain_from_millibel((int)b->volume - (b->pan > 0 ? b->pan : 0));
        right = gain_from_millibel((int)b->volume - (b->pan < 0 ? -b->pan : 0));
        for (i = 0; i < frames && b->playing; i++) {
            int l = sample_at(b, b->frame, 0), r = sample_at(b, b->frame, 1);
            int o = (int)i * 2;
            int mixed_l = out[o] + (int)(((long long)l * left) >> 16);
            int mixed_r = out[o + 1] + (int)(((long long)r * right) >> 16);
            out[o] = (short)(mixed_l > 32767 ? 32767 : mixed_l < -32768 ? -32768 : mixed_l);
            out[o + 1] = (short)(mixed_r > 32767 ? 32767 : mixed_r < -32768 ? -32768 : mixed_r);
            b->frac += step;
            b->frame += b->frac >> 12;
            b->frac &= 4095;
            if (b->frame >= total) {
                if (b->looping) {
                    b->frame %= total;
                } else {
                    b->frame = total;
                    b->playing = 0;
                }
            }
        }
        b->position = b->frame * align;
    }
    return frames;
}

static pe32_u32 PE32_WINAPI b_get_caps(A self, unsigned char *caps)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    if (!caps) {
        return DSERR_INVALIDPARAM;
    }
    clear(caps, 20);
    *(pe32_u32 *)caps = 20;
    *(pe32_u32 *)(caps + 4) = b->flags;
    *(pe32_u32 *)(caps + 8) = b->bytes;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_current_position(A self, A *play, A *write)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    if (play) *play = b->position;
    if (write) *write = b->position;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_format(A self, unsigned char *fmt, A size,
                                         A *written)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    A i;
    for (i = 0; fmt && i < size && i < 18; i++) {
        fmt[i] = b->format[i];
    }
    if (written) *written = i;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_volume(A self, A *v)
{
    if (v) *v = ((struct buffer *)(unsigned long)self)->volume;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_pan(A self, A *p)
{
    if (p) *p = (A)((struct buffer *)(unsigned long)self)->pan;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_frequency(A self, A *f)
{
    if (f) *f = ((struct buffer *)(unsigned long)self)->frequency;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_get_status(A self, A *s)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    if (s) *s = b->playing ? (1 | (b->looping ? 4 : 0)) : 0;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_initialize(A self, A ds, const void *desc)
{
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_lock(A self, A offset, A bytes, A *p1, A *n1,
                                   A *p2, A *n2, A flags)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    if (flags & 2) { /* DSBLOCK_ENTIREBUFFER */
        offset = 0;
        bytes = b->bytes;
    }
    if (offset > b->bytes || !p1 || !n1) {
        return DSERR_INVALIDPARAM;
    }
    if (bytes > b->bytes) {
        bytes = b->bytes;
    }
    if (bytes > b->bytes - offset) {
        /* Wrap the remainder to the start of the ring. */
        *p1 = (A)(unsigned long)(b->data + offset);
        *n1 = b->bytes - offset;
        if (p2) *p2 = (A)(unsigned long)b->data;
        if (n2) *n2 = bytes - *n1;
    } else {
        *p1 = (A)(unsigned long)(b->data + offset);
        *n1 = bytes;
        if (p2) *p2 = 0;
        if (n2) *n2 = 0;
    }
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_play(A self, A reserved, A priority, A flags)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    b->playing = 1;
    b->looping = (flags & 1) != 0;
    if (!b->looping && b->position >= b->bytes) {
        b->position = 0;
        b->frame = b->frac = 0;
    }
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_set_current_position(A self, A pos)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    pe32_u32 align = block_align(b);
    b->position = pos;
    b->frame = pos / align;
    b->frac = 0;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_set_format(A self, const unsigned char *fmt)
{
    struct buffer *b = (struct buffer *)(unsigned long)self;
    int i;
    for (i = 0; fmt && i < 16; i++) {
        b->format[i] = fmt[i];
    }
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_set_volume(A self, A v)
{
    ((struct buffer *)(unsigned long)self)->volume = v;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_set_pan(A self, A p)
{
    ((struct buffer *)(unsigned long)self)->pan = (int)p;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_set_frequency(A self, A f)
{
    ((struct buffer *)(unsigned long)self)->frequency = f ? f : 22050;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_stop(A self)
{
    ((struct buffer *)(unsigned long)self)->playing = 0;
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_unlock(A self, A p1, A n1, A p2, A n2)
{
    return DS_OK;
}

static pe32_u32 PE32_WINAPI b_restore(A self) { return DS_OK; }

#define V(t, i, fn) (t)[i] = (A)(unsigned long)&(fn)

static void init_vtables(void)
{
    if (ready) {
        return;
    }
    ready = 1;
    V(vtable_ds, 0, query_interface); V(vtable_ds, 1, com_add_ref);
    V(vtable_ds, 2, com_release); V(vtable_ds, 3, ds_create_sound_buffer);
    V(vtable_ds, 4, ds_get_caps); V(vtable_ds, 5, ds_duplicate);
    V(vtable_ds, 6, ds_set_cooperative_level); V(vtable_ds, 7, ds_compact);
    V(vtable_ds, 8, ds_get_speaker_config); V(vtable_ds, 9, ds_set_speaker_config);
    V(vtable_ds, 10, ds_initialize);
    ds_object.vtbl = (A)(unsigned long)vtable_ds;
    V(vtable_buffer, 0, query_interface); V(vtable_buffer, 1, com_add_ref);
    V(vtable_buffer, 2, com_release); V(vtable_buffer, 3, b_get_caps);
    V(vtable_buffer, 4, b_get_current_position); V(vtable_buffer, 5, b_get_format);
    V(vtable_buffer, 6, b_get_volume); V(vtable_buffer, 7, b_get_pan);
    V(vtable_buffer, 8, b_get_frequency); V(vtable_buffer, 9, b_get_status);
    V(vtable_buffer, 10, b_initialize); V(vtable_buffer, 11, b_lock);
    V(vtable_buffer, 12, b_play); V(vtable_buffer, 13, b_set_current_position);
    V(vtable_buffer, 14, b_set_format); V(vtable_buffer, 15, b_set_volume);
    V(vtable_buffer, 16, b_set_pan); V(vtable_buffer, 17, b_set_frequency);
    V(vtable_buffer, 18, b_stop); V(vtable_buffer, 19, b_unlock);
    V(vtable_buffer, 20, b_restore);
}

pe32_u32 PE32_WINAPI pe32_dsound_create(const void *guid, pe32_u32 *out,
                                        pe32_u32 outer)
{
    if (!out) {
        return DSERR_INVALIDPARAM;
    }
    init_vtables();
    *out = (A)(unsigned long)&ds_object;
    return DS_OK;
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

enum pe32_link_status pe32_dsound_resolve(const char *name, pe32_u32 *address)
{
#if defined(__i386__)
    if (names_equal(name, "DirectSoundCreate")) {
        *address = (A)(unsigned long)&pe32_dsound_create;
        return PE32_LINK_OK;
    }
#else
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}

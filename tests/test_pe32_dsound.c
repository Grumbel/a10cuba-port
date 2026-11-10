/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* i386-only and freestanding (no 32-bit libc needed): the DirectSound
 * objects hold 32-bit guest pointers, so this runs under qemu-i386. */
#include "pe32_dsound.h"

static void quit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {
    }
}

#define assert(cond) do { if (!(cond)) quit(__LINE__); } while (0)

static void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) {
        *a++ = *b++;
    }
    return d;
}

static void *memset(void *d, int v, unsigned n)
{
    unsigned char *a = d;
    while (n--) {
        *a++ = (unsigned char)v;
    }
    return d;
}

typedef pe32_u32 (PE32_WINAPI *fn1)(pe32_u32);

static pe32_u32 call(pe32_u32 object, int slot)
{
    pe32_u32 *vtbl = (pe32_u32 *)(unsigned long)*(pe32_u32 *)(unsigned long)object;
    return ((fn1)(unsigned long)vtbl[slot])(object);
}

static int run(void)
{
    pe32_u32 ds = 0, buffer = 0;
    pe32_u32 desc[6] = {20, 0xe0, 4, 0, 0, 0};
    unsigned char fmt[16] = {1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0};
    pe32_u32 *vtbl, p1, n1, p2, n2, pos;
    short out[16];
    short samples[2] = {1000, -2000};

    assert(pe32_dsound_create(0, &ds, 0) == 0);
    vtbl = (pe32_u32 *)(unsigned long)*(pe32_u32 *)(unsigned long)ds;
    desc[3] = 0;
    desc[4] = (pe32_u32)(unsigned long)fmt;
    desc[2] = 4; /* two 16-bit mono frames at 44100 Hz */
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, const void *, pe32_u32 *, pe32_u32))
            (unsigned long)vtbl[3])(ds, desc, &buffer, 0) == 0);
    vtbl = (pe32_u32 *)(unsigned long)*(pe32_u32 *)(unsigned long)buffer;
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32, pe32_u32, pe32_u32 *,
                                     pe32_u32 *, pe32_u32 *, pe32_u32 *, pe32_u32))
            (unsigned long)vtbl[11])(buffer, 0, 4, &p1, &n1, &p2, &n2, 0) == 0);
    memcpy((void *)(unsigned long)p1, samples, 4);
    memset(out, 0x55, sizeof out);
    pe32_dsound_mix(out, 4);
    assert(out[0] == 0 && out[1] == 0);          /* not playing yet */
    /* Play(reserved, priority, flags): looping. */
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32, pe32_u32, pe32_u32))
            (unsigned long)vtbl[12])(buffer, 0, 0, 1) == 0);
    pe32_dsound_mix(out, 4);
    assert(out[0] == 1000 && out[1] == 1000);    /* mono -> both channels */
    assert(out[2] == -2000 && out[3] == -2000);
    assert(out[4] == 0);                         /* buffer is padded to 16 bytes */
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32 *, pe32_u32 *))
            (unsigned long)vtbl[4])(buffer, &pos, &p2) == 0);
    assert(pos == 8);                            /* 4 frames * 2 bytes */
    pe32_dsound_mix(out, 8);                     /* wraps the 8-frame ring */
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32 *, pe32_u32 *))
            (unsigned long)vtbl[4])(buffer, &pos, &p2) == 0);
    assert(pos == 8);
    pe32_dsound_mix(out, 4);
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32))
            (unsigned long)vtbl[13])(buffer, 0) == 0);
    /* SetVolume(-600) is about half amplitude. */
    assert(((pe32_u32(PE32_WINAPI *)(pe32_u32, pe32_u32))
            (unsigned long)vtbl[15])(buffer, (pe32_u32)-602) == 0);
    pe32_dsound_mix(out, 2);
    assert(out[0] >= 490 && out[0] <= 510);
    (void)call;
    return 0;
}

void _start(void)
{
    quit(run());
}

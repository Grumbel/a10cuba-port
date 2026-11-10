/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_DDRAW_H
#define A10_PE32_DDRAW_H

#include "pe32_win32.h"

/* Software DirectDraw: IDirectDraw/IDirectDraw2, 8-bit palettized surfaces
 * held in guest-visible memory, and IDirectDrawPalette. The primary surface is
 * presented to the host through a callback. */

struct pe32_ddraw_frame {
    const unsigned char *pixels; /* 8-bit indices */
    pe32_u32 width, height, pitch;
    const unsigned char *palette; /* 256 RGBA-order entries (r,g,b,flags) */
};

typedef void (*pe32_ddraw_present_fn)(const struct pe32_ddraw_frame *frame);
void pe32_ddraw_set_present(pe32_ddraw_present_fn present);

pe32_u32 PE32_WINAPI pe32_ddraw_create(const void *guid, pe32_u32 *out,
                                       pe32_u32 outer);

enum pe32_link_status pe32_ddraw_resolve(const char *name, pe32_u32 *address);

#endif

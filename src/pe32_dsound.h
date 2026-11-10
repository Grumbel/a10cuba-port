/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_DSOUND_H
#define A10_PE32_DSOUND_H

#include "pe32_win32.h"

/* DirectSound: IDirectSound and IDirectSoundBuffer with real buffer memory
 * and a software mixer. pe32_dsound_mix renders `frames` frames of stereo
 * signed 16-bit audio at 44100 Hz from the playing buffers (volume, pan,
 * frequency and looping honoured) and advances their play positions. */
#define PE32_DSOUND_RATE 44100
unsigned pe32_dsound_mix(short *out, unsigned frames);
pe32_u32 PE32_WINAPI pe32_dsound_create(const void *guid, pe32_u32 *out,
                                        pe32_u32 outer);

enum pe32_link_status pe32_dsound_resolve(const char *name, pe32_u32 *address);

#endif

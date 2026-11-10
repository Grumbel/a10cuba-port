/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_DSOUND_H
#define A10_PE32_DSOUND_H

#include "pe32_win32.h"

/* Silent DirectSound: IDirectSound and IDirectSoundBuffer with real buffer
 * memory (Lock/Unlock work) but no audio output yet. */
pe32_u32 PE32_WINAPI pe32_dsound_create(const void *guid, pe32_u32 *out,
                                        pe32_u32 outer);

enum pe32_link_status pe32_dsound_resolve(const char *name, pe32_u32 *address);

#endif

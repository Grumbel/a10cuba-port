/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_RES_H
#define A10_PE32_RES_H

#include "pe32_win32.h"

/* PE resource lookup in mapped images. An HRSRC is the guest address of the
 * IMAGE_RESOURCE_DATA_ENTRY; an HGLOBAL from LoadResource is the guest
 * address of the data itself. Only the first language of a name is returned.
 * Names may be MAKEINTRESOURCE integers, "#123" strings or ASCII names. */
pe32_u32 PE32_WINAPI pe32_win32_find_resource_a(pe32_u32 module,
                                                const char *name,
                                                const char *type);
pe32_u32 PE32_WINAPI pe32_win32_load_resource(pe32_u32 module, pe32_u32 res);
pe32_u32 PE32_WINAPI pe32_win32_lock_resource(pe32_u32 data);
pe32_u32 PE32_WINAPI pe32_win32_sizeof_resource(pe32_u32 module, pe32_u32 res);
pe32_u32 PE32_WINAPI pe32_win32_free_resource(pe32_u32 data);

#endif

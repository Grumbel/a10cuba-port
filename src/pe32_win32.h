/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_WIN32_H
#define A10_PE32_WIN32_H

#include "pe32_link.h"

#if defined(__i386__) && defined(__GNUC__)
#define PE32_WINAPI __attribute__((stdcall))
#else
#define PE32_WINAPI
#endif

/* Experimental single-threaded registry for PRELOADED PE images only.
 * This does not implement DLL search/loading, DllMain, reference counts,
 * GetLastError or the Windows process environment. */
void pe32_win32_initialize(const struct pe32_module *modules, pe32_u32 count);

/* HMODULE is the original image base, exactly as in 32-bit Windows. */
pe32_u32 PE32_WINAPI pe32_win32_load_library_a(const char *name);
pe32_u32 PE32_WINAPI pe32_win32_get_module_handle_a(const char *name);
pe32_u32 PE32_WINAPI pe32_win32_get_proc_address(pe32_u32 handle,
                                                  const char *name_or_ordinal);

/* Resolve just the three actual i386 stubs above for Kernel32 IAT imports.
 * On a non-i386 host, the functions can be tested but not exposed as 32-bit
 * callable addresses. Unknown APIs always remain unresolved. */
enum pe32_link_status pe32_win32_resolve_import(
    void *context, const char *dll, const char *name, pe32_u32 ordinal,
    pe32_u32 *address);

#endif

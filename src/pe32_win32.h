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
 * per-thread TLS/last-error isolation or the Windows process environment. */
void pe32_win32_initialize(const struct pe32_module *modules, pe32_u32 count);

/* HMODULE is the original image base, exactly as in 32-bit Windows. */
pe32_u32 PE32_WINAPI pe32_win32_load_library_a(const char *name);
pe32_u32 PE32_WINAPI pe32_win32_get_module_handle_a(const char *name);
pe32_u32 PE32_WINAPI pe32_win32_get_proc_address(pe32_u32 handle,
                                                  const char *name_or_ordinal);

/* Single-process startup records for early CRT calls. Paths are the supplied
 * PE module paths (not yet translated to a virtual Windows drive). The guest
 * sees an empty, read-only environment; the Linux host environment is never
 * leaked through these functions. STARTUPINFOA occupies 68 bytes on PE32. */
#define PE32_STARTUPINFOA_SIZE 68u
char *PE32_WINAPI pe32_win32_get_command_line_a(void);
pe32_u32 PE32_WINAPI pe32_win32_get_module_file_name_a(pe32_u32 module,
                                                      char *buffer,
                                                      pe32_u32 capacity);
void PE32_WINAPI pe32_win32_get_startup_info_a(void *startup_info);
char *PE32_WINAPI pe32_win32_get_environment_strings(void);
pe32_u16 *PE32_WINAPI pe32_win32_get_environment_strings_w(void);
pe32_u32 PE32_WINAPI pe32_win32_free_environment_strings_a(char *environment);
pe32_u32 PE32_WINAPI pe32_win32_free_environment_strings_w(pe32_u16 *environment);
pe32_u32 PE32_WINAPI pe32_win32_get_current_process(void);
pe32_u32 PE32_WINAPI pe32_win32_get_current_thread(void);
pe32_u32 PE32_WINAPI pe32_win32_get_current_thread_id(void);

/* Single-thread subset of Win32 last error and dynamically allocated TLS.
 * TLS values are 32-bit guest pointers/integers. These must NOT be used with
 * multiple Linux threads until a real per-thread implementation exists. */
#define PE32_TLS_OUT_OF_INDEXES 0xffffffffu
void PE32_WINAPI pe32_win32_set_last_error(pe32_u32 error);
pe32_u32 PE32_WINAPI pe32_win32_get_last_error(void);
pe32_u32 PE32_WINAPI pe32_win32_tls_alloc(void);
pe32_u32 PE32_WINAPI pe32_win32_tls_free(pe32_u32 index);
pe32_u32 PE32_WINAPI pe32_win32_tls_get_value(pe32_u32 index);
pe32_u32 PE32_WINAPI pe32_win32_tls_set_value(pe32_u32 index, pe32_u32 value);

/* Resolve just the explicitly implemented i386 stubs for Kernel32 imports.
 * On a non-i386 host, the functions can be tested but not exposed as 32-bit
 * callable addresses. Unknown APIs always remain unresolved. */
enum pe32_link_status pe32_win32_resolve_import(
    void *context, const char *dll, const char *name, pe32_u32 ordinal,
    pe32_u32 *address);

#endif

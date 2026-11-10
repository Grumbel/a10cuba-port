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

/* Registered module for an HMODULE (0 = the main EXE), or NULL. */
const struct pe32_module *pe32_win32_module_for_handle(pe32_u32 handle);

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

/* ANSI byte-string helpers imported by the original EXE. Callers must
 * provide valid, non-overlapping buffers (there is no Win32 SEH yet).
 * lstrcpynA's count includes the trailing NUL; nonpositive counts fail. */
int PE32_WINAPI pe32_win32_lstrlen_a(const char *string);
char *PE32_WINAPI pe32_win32_lstrcpy_a(char *dest, const char *source);
char *PE32_WINAPI pe32_win32_lstrcpyn_a(char *dest, const char *source,
                                        int max_length);
char *PE32_WINAPI pe32_win32_lstrcat_a(char *dest, const char *source);

/* 32-bit LONG operations with sequentially consistent atomic ordering.
 * The caller's variable must be naturally 32-bit aligned. */
pe32_u32 PE32_WINAPI pe32_win32_interlocked_increment(volatile pe32_u32 *value);
pe32_u32 PE32_WINAPI pe32_win32_interlocked_decrement(volatile pe32_u32 *value);

/* The Windows NT/i386 RTL_CRITICAL_SECTION layout is exactly 24 bytes.
 * Supported only while this runtime has one synthetic Windows thread; its
 * bookkeeping permits recursive acquisition but is NOT a multi-thread lock.
 * DebugInfo/LockSemaphore are opaque 32-bit Windows pointers/handles. */
struct pe32_critical_section {
    pe32_u32 debug_info;
    pe32_u32 lock_count;
    pe32_u32 recursion_count;
    pe32_u32 owning_thread;
    pe32_u32 lock_semaphore;
    pe32_u32 spin_count;
};

void PE32_WINAPI pe32_win32_initialize_critical_section(
    struct pe32_critical_section *section);
void PE32_WINAPI pe32_win32_enter_critical_section(
    struct pe32_critical_section *section);
void PE32_WINAPI pe32_win32_leave_critical_section(
    struct pe32_critical_section *section);
void PE32_WINAPI pe32_win32_delete_critical_section(
    struct pe32_critical_section *section);

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

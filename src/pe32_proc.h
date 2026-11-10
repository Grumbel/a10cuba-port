/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_PROC_H
#define A10_PE32_PROC_H

#include "pe32_win32.h"

/* Process, memory-handle, event and thread shims for the single-threaded
 * runtime. Process exit and sleeping are delegated to a host backend so the
 * code stays freestanding and unit-testable. */
typedef void (*pe32_proc_exit_fn)(pe32_u32 code);
typedef void (*pe32_proc_sleep_fn)(pe32_u32 milliseconds);
void pe32_proc_set_backend(pe32_proc_exit_fn exit_fn,
                           pe32_proc_sleep_fn sleep_fn);
void pe32_proc_reset(void);

/* Returns non-zero if the handle belonged to an event (and closes it). */
int pe32_proc_close_handle(pe32_u32 handle, pe32_u32 *result);

/* GlobalAlloc/LocalAlloc family: every block is fixed, so a "handle" is the
 * pointer itself and GlobalLock is the identity. */
#define PE32_GMEM_ZEROINIT 0x40u
void *PE32_WINAPI pe32_win32_global_alloc(pe32_u32 flags, pe32_u32 bytes);
void *PE32_WINAPI pe32_win32_global_realloc(void *memory, pe32_u32 bytes,
                                            pe32_u32 flags);
void *PE32_WINAPI pe32_win32_global_free(void *memory);
void *PE32_WINAPI pe32_win32_global_lock(void *memory);
pe32_u32 PE32_WINAPI pe32_win32_global_unlock(void *memory);
void *PE32_WINAPI pe32_win32_global_handle(void *memory);
pe32_u32 PE32_WINAPI pe32_win32_global_flags(void *memory);
void PE32_WINAPI pe32_win32_global_memory_status(void *status);

void PE32_WINAPI pe32_win32_exit_process(pe32_u32 code);
pe32_u32 PE32_WINAPI pe32_win32_terminate_process(pe32_u32 process,
                                                  pe32_u32 code);
void PE32_WINAPI pe32_win32_exit_thread(pe32_u32 code);
void PE32_WINAPI pe32_win32_sleep(pe32_u32 milliseconds);
void PE32_WINAPI pe32_win32_output_debug_string_a(const char *text);
pe32_u32 PE32_WINAPI pe32_win32_set_error_mode(pe32_u32 mode);
pe32_u32 PE32_WINAPI pe32_win32_set_unhandled_exception_filter(pe32_u32 filter);
pe32_u32 PE32_WINAPI pe32_win32_unhandled_exception_filter(void *info);
void PE32_WINAPI pe32_win32_raise_exception(pe32_u32 code, pe32_u32 flags,
                                            pe32_u32 count, const void *args);
void PE32_WINAPI pe32_win32_rtl_unwind(void *frame, void *target,
                                       void *record, pe32_u32 value);
pe32_u32 PE32_WINAPI pe32_win32_virtual_protect(void *address, pe32_u32 size,
                                                pe32_u32 protect,
                                                pe32_u32 *old_protect);
pe32_u32 PE32_WINAPI pe32_win32_free_library(pe32_u32 module);
pe32_u32 PE32_WINAPI pe32_win32_flush_file_buffers(pe32_u32 handle);

/* Manual/auto-reset events. Only a signalled event (or a zero timeout) can
 * be waited on successfully: there is no second thread to signal it. */
#define PE32_WAIT_OBJECT_0 0u
#define PE32_WAIT_TIMEOUT 258u
#define PE32_WAIT_FAILED 0xffffffffu
pe32_u32 PE32_WINAPI pe32_win32_create_event_a(const void *security,
                                               pe32_u32 manual_reset,
                                               pe32_u32 initial_state,
                                               const char *name);
pe32_u32 PE32_WINAPI pe32_win32_set_event(pe32_u32 handle);
pe32_u32 PE32_WINAPI pe32_win32_wait_for_single_object(pe32_u32 handle,
                                                       pe32_u32 timeout);

/* Threads are unsupported: creation fails, other calls fail benignly. */
pe32_u32 PE32_WINAPI pe32_win32_create_thread(const void *security,
                                              pe32_u32 stack, pe32_u32 start,
                                              pe32_u32 parameter,
                                              pe32_u32 flags, pe32_u32 *id);
pe32_u32 PE32_WINAPI pe32_win32_suspend_thread(pe32_u32 thread);
pe32_u32 PE32_WINAPI pe32_win32_resume_thread(pe32_u32 thread);
pe32_u32 PE32_WINAPI pe32_win32_set_thread_priority(pe32_u32 thread,
                                                    pe32_u32 priority);
pe32_u32 PE32_WINAPI pe32_win32_duplicate_handle(pe32_u32 src_process,
                                                 pe32_u32 src,
                                                 pe32_u32 dst_process,
                                                 pe32_u32 *dst, pe32_u32 access,
                                                 pe32_u32 inherit,
                                                 pe32_u32 options);

#endif

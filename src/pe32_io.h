/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_IO_H
#define A10_PE32_IO_H

#include "pe32_win32.h"

#define PE32_STD_INPUT_HANDLE  0xfffffff6u
#define PE32_STD_OUTPUT_HANDLE 0xfffffff5u
#define PE32_STD_ERROR_HANDLE  0xfffffff4u
#define PE32_INVALID_HANDLE_VALUE 0xffffffffu
#define PE32_FILE_TYPE_UNKNOWN 0u
#define PE32_FILE_TYPE_DISK 1u
#define PE32_FILE_TYPE_CHAR 2u
#define PE32_FILE_TYPE_PIPE 3u

/* The first-stage I/O adapter only maps inherited standard handles (Linux
 * fds 0, 1 and 2). Callbacks return a byte count or negative Linux errno;
 * file_type returns a PE32_FILE_TYPE_* value or negative Linux errno.
 * The host supplies syscalls; synthetic host tests inject fake I/O. */
typedef int (*pe32_io_read_fn)(pe32_u32 fd, void *buffer, pe32_u32 count);
typedef int (*pe32_io_write_fn)(pe32_u32 fd, const void *buffer, pe32_u32 count);
typedef int (*pe32_io_type_fn)(pe32_u32 fd);

void pe32_io_set_backend(pe32_io_read_fn read_fn, pe32_io_write_fn write_fn,
                         pe32_io_type_fn type_fn);
void pe32_io_reset(void);

pe32_u32 PE32_WINAPI pe32_win32_get_std_handle(pe32_u32 selector);
pe32_u32 PE32_WINAPI pe32_win32_set_std_handle(pe32_u32 selector, pe32_u32 handle);
pe32_u32 PE32_WINAPI pe32_win32_set_handle_count(pe32_u32 count);
pe32_u32 PE32_WINAPI pe32_win32_get_file_type(pe32_u32 handle);
pe32_u32 PE32_WINAPI pe32_win32_read_file(pe32_u32 handle, void *buffer,
                                         pe32_u32 count, pe32_u32 *read_count,
                                         void *overlapped);
pe32_u32 PE32_WINAPI pe32_win32_write_file(pe32_u32 handle, const void *buffer,
                                          pe32_u32 count, pe32_u32 *written_count,
                                          void *overlapped);
pe32_u32 PE32_WINAPI pe32_win32_close_handle(pe32_u32 handle);

#endif

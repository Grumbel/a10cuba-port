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

/* Standard handles map to Linux fds 0, 1 and 2; additional handles reference
 * read-only game files. Callbacks return results or negative Linux errno.
 * The host supplies syscalls; synthetic host tests inject fake I/O. */
typedef int (*pe32_io_read_fn)(pe32_u32 fd, void *buffer, pe32_u32 count);
typedef int (*pe32_io_write_fn)(pe32_u32 fd, const void *buffer, pe32_u32 count);
typedef int (*pe32_io_type_fn)(pe32_u32 fd);
typedef int (*pe32_io_open_read_fn)(const char *root, const char *relative);
typedef int (*pe32_io_close_fn)(pe32_u32 fd);
typedef int (*pe32_io_seek_fn)(pe32_u32 fd, pe32_u32 low, pe32_u32 high,
                               pe32_u32 method, pe32_u32 *result_low,
                               pe32_u32 *result_high);
typedef int (*pe32_io_size_fn)(pe32_u32 fd, pe32_u32 *low, pe32_u32 *high);

void pe32_io_set_backend(pe32_io_read_fn read_fn, pe32_io_write_fn write_fn,
                         pe32_io_type_fn type_fn);
void pe32_io_set_file_backend(pe32_io_open_read_fn open_read,
                              pe32_io_close_fn close_fn,
                              pe32_io_seek_fn seek_fn,
                              pe32_io_size_fn size_fn);
void pe32_io_reset(void);
void pe32_io_set_image_path(const char *image_path);

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
pe32_u32 PE32_WINAPI pe32_win32_create_file_a(
    const char *path, pe32_u32 access, pe32_u32 share_mode,
    const void *security, pe32_u32 disposition, pe32_u32 flags,
    pe32_u32 template_handle);
pe32_u32 PE32_WINAPI pe32_win32_get_file_size(pe32_u32 handle, pe32_u32 *high);
pe32_u32 PE32_WINAPI pe32_win32_set_file_pointer(pe32_u32 handle,
                                                   pe32_u32 distance,
                                                   pe32_u32 *distance_high,
                                                   pe32_u32 method);

#endif

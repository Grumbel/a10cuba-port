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

/* Win32 FILETIME and BY_HANDLE_FILE_INFORMATION are fixed-size PE32 records.
 * Linux backends return raw Unix seconds/nanoseconds; the shim converts them. */
struct pe32_filetime {
    pe32_u32 low;
    pe32_u32 high;
};

struct pe32_file_information {
    pe32_u32 attributes;
    struct pe32_filetime creation_time;
    struct pe32_filetime access_time;
    struct pe32_filetime write_time;
    pe32_u32 volume_serial;
    pe32_u32 size_high;
    pe32_u32 size_low;
    pe32_u32 number_of_links;
    pe32_u32 index_high;
    pe32_u32 index_low;
};

struct pe32_io_stat {
    pe32_u32 mode;
    pe32_u32 links;
    pe32_u32 size_low;
    pe32_u32 size_high;
    pe32_u32 inode_low;
    pe32_u32 inode_high;
    pe32_u32 access_seconds;
    pe32_u32 access_nanoseconds;
    pe32_u32 write_seconds;
    pe32_u32 write_nanoseconds;
};

struct pe32_find_data_a {
    pe32_u32 attributes;
    struct pe32_filetime creation_time;
    struct pe32_filetime access_time;
    struct pe32_filetime write_time;
    pe32_u32 size_high;
    pe32_u32 size_low;
    pe32_u32 reserved0;
    pe32_u32 reserved1;
    char filename[260];
    char alternate_filename[14];
};

typedef int (*pe32_io_stat_fn)(pe32_u32 fd, struct pe32_io_stat *stat);
typedef int (*pe32_io_open_dir_fn)(const char *root, const char *relative);
/* Returns 1 for a directory entry, 0 at EOF, or negative Linux errno.
 * The entry name is a NUL-terminated filename (not a path). */
typedef int (*pe32_io_next_dir_fn)(pe32_u32 fd, char *name,
                                   pe32_u32 capacity, struct pe32_io_stat *stat);

void pe32_io_set_backend(pe32_io_read_fn read_fn, pe32_io_write_fn write_fn,
                         pe32_io_type_fn type_fn);
void pe32_io_set_file_backend(pe32_io_open_read_fn open_read,
                              pe32_io_close_fn close_fn,
                              pe32_io_seek_fn seek_fn,
                              pe32_io_size_fn size_fn);
void pe32_io_set_stat_backend(pe32_io_stat_fn stat_fn);
void pe32_io_set_directory_backend(pe32_io_open_dir_fn open_dir,
                                   pe32_io_next_dir_fn next_dir);
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
pe32_u32 PE32_WINAPI pe32_win32_get_full_path_name_a(
    const char *filename, pe32_u32 capacity, char *buffer, char **file_part);
pe32_u32 PE32_WINAPI pe32_win32_create_file_a(
    const char *path, pe32_u32 access, pe32_u32 share_mode,
    const void *security, pe32_u32 disposition, pe32_u32 flags,
    pe32_u32 template_handle);
pe32_u32 PE32_WINAPI pe32_win32_get_file_size(pe32_u32 handle, pe32_u32 *high);
pe32_u32 PE32_WINAPI pe32_win32_set_file_pointer(pe32_u32 handle,
                                                   pe32_u32 distance,
                                                   pe32_u32 *distance_high,
                                                   pe32_u32 method);
pe32_u32 PE32_WINAPI pe32_win32_get_file_time(
    pe32_u32 handle, struct pe32_filetime *creation,
    struct pe32_filetime *access, struct pe32_filetime *write);
pe32_u32 PE32_WINAPI pe32_win32_get_file_information_by_handle(
    pe32_u32 handle, struct pe32_file_information *information);
pe32_u32 PE32_WINAPI pe32_win32_find_first_file_a(
    const char *pattern, struct pe32_find_data_a *data);
pe32_u32 PE32_WINAPI pe32_win32_find_next_file_a(
    pe32_u32 handle, struct pe32_find_data_a *data);
pe32_u32 PE32_WINAPI pe32_win32_find_close(pe32_u32 handle);

#endif

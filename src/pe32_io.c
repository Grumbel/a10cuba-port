/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Inherited standard I/O, without Win32 filesystem or asynchronous I/O.
 * An explicit handle table prevents guest handles from being mistaken for
 * Linux fds, and allows SetStdHandle to alias them without ownership bugs. */

#include "pe32_io.h"

enum {
    ERROR_SUCCESS = 0,
    ERROR_INVALID_HANDLE = 6,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_BROKEN_PIPE = 109,
    ERROR_DISK_FULL = 112,
    ERROR_GEN_FAILURE = 31,
    ERROR_NOACCESS = 998,
    ERROR_OPERATION_ABORTED = 995,
    ERROR_NO_DATA = 232,
    LINUX_EINTR = 4,
    LINUX_EBADF = 9,
    LINUX_EAGAIN = 11,
    LINUX_EFAULT = 14,
    LINUX_ENOSPC = 28,
    LINUX_EPIPE = 32,
    HANDLE_BASE = 0x1000,
    HANDLE_STEP = 4,
    STD_HANDLE_COUNT = 3
};

static pe32_io_read_fn backend_read;
static pe32_io_write_fn backend_write;
static pe32_io_type_fn backend_type;
static pe32_u32 current_std_handles[STD_HANDLE_COUNT];
static pe32_u8 open_std_handles[STD_HANDLE_COUNT];

static int index_for_selector(pe32_u32 selector)
{
    if (selector == PE32_STD_INPUT_HANDLE) {
        return 0;
    }
    if (selector == PE32_STD_OUTPUT_HANDLE) {
        return 1;
    }
    return selector == PE32_STD_ERROR_HANDLE ? 2 : -1;
}

static int index_for_handle(pe32_u32 handle)
{
    pe32_u32 index;
    if (handle < HANDLE_BASE || handle >= HANDLE_BASE + 3 * HANDLE_STEP ||
        (handle - HANDLE_BASE) % HANDLE_STEP) {
        return -1;
    }
    index = (handle - HANDLE_BASE) / HANDLE_STEP;
    return open_std_handles[index] ? (int)index : -1;
}

static pe32_u32 win32_io_error(int linux_errno)
{
    switch (linux_errno) {
    case LINUX_EBADF: return ERROR_INVALID_HANDLE;
    case LINUX_EINTR: return ERROR_OPERATION_ABORTED;
    case LINUX_EAGAIN: return ERROR_NO_DATA;
    case LINUX_EFAULT: return ERROR_NOACCESS;
    case LINUX_ENOSPC: return ERROR_DISK_FULL;
    case LINUX_EPIPE: return ERROR_BROKEN_PIPE;
    default: return ERROR_GEN_FAILURE;
    }
}

void pe32_io_set_backend(pe32_io_read_fn read_fn, pe32_io_write_fn write_fn,
                         pe32_io_type_fn type_fn)
{
    backend_read = read_fn;
    backend_write = write_fn;
    backend_type = type_fn;
}

void pe32_io_reset(void)
{
    pe32_u32 i;
    for (i = 0; i < STD_HANDLE_COUNT; ++i) {
        current_std_handles[i] = HANDLE_BASE + i * HANDLE_STEP;
        open_std_handles[i] = 1;
    }
}

pe32_u32 PE32_WINAPI pe32_win32_get_std_handle(pe32_u32 selector)
{
    int index = index_for_selector(selector);
    if (index < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return PE32_INVALID_HANDLE_VALUE;
    }
    return current_std_handles[index];
}

pe32_u32 PE32_WINAPI pe32_win32_set_std_handle(pe32_u32 selector,
                                               pe32_u32 handle)
{
    int index = index_for_selector(selector);
    if (index < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Windows stores the handle value without verifying its validity. */
    current_std_handles[index] = handle;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_set_handle_count(pe32_u32 count)
{
    /* Obsolete on NT Win32: no actual per-process fd limit adjustment. */
    return count;
}

pe32_u32 PE32_WINAPI pe32_win32_get_file_type(pe32_u32 handle)
{
    int index = index_for_handle(handle);
    int type;
    if (index < 0 || !backend_type) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return PE32_FILE_TYPE_UNKNOWN;
    }
    type = backend_type((pe32_u32)index);
    if (type < 0) {
        pe32_win32_set_last_error(win32_io_error(-type));
        return PE32_FILE_TYPE_UNKNOWN;
    }
    if (type == PE32_FILE_TYPE_UNKNOWN) {
        pe32_win32_set_last_error(ERROR_SUCCESS);
    }
    return (pe32_u32)type;
}

pe32_u32 PE32_WINAPI pe32_win32_read_file(pe32_u32 handle, void *buffer,
                                         pe32_u32 count, pe32_u32 *read_count,
                                         void *overlapped)
{
    int index = index_for_handle(handle);
    int result;
    if (read_count) {
        *read_count = 0;
    }
    if (overlapped || !read_count || (!buffer && count)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (index < 0 || !backend_read) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!count) {
        return 1;
    }
    result = backend_read((pe32_u32)index, buffer, count);
    if (result < 0) {
        pe32_win32_set_last_error(win32_io_error(-result));
        return 0;
    }
    if ((pe32_u32)result > count) {
        pe32_win32_set_last_error(ERROR_GEN_FAILURE);
        return 0;
    }
    *read_count = (pe32_u32)result;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_write_file(pe32_u32 handle, const void *buffer,
                                          pe32_u32 count, pe32_u32 *written_count,
                                          void *overlapped)
{
    int index = index_for_handle(handle);
    int result;
    if (written_count) {
        *written_count = 0;
    }
    if (overlapped || !written_count || (!buffer && count)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (index < 0 || !backend_write) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!count) {
        return 1;
    }
    result = backend_write((pe32_u32)index, buffer, count);
    if (result < 0) {
        pe32_win32_set_last_error(win32_io_error(-result));
        return 0;
    }
    if ((pe32_u32)result > count) {
        pe32_win32_set_last_error(ERROR_GEN_FAILURE);
        return 0;
    }
    *written_count = (pe32_u32)result;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_close_handle(pe32_u32 handle)
{
    int index = index_for_handle(handle);
    if (index < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    /* Do not close the Linux process's actual stdin/stdout/stderr: the native
     * loader uses those descriptors for its own diagnostics. */
    open_std_handles[index] = 0;
    return 1;
}

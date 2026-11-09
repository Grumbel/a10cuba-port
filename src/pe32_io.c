/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Inherited standard I/O and read-only game data, without Win32 writes.
 * An explicit handle table prevents guest handles from being mistaken for
 * Linux fds, and allows SetStdHandle to alias them without ownership bugs. */

#include "pe32_io.h"

enum {
    ERROR_SUCCESS = 0,
    ERROR_INVALID_HANDLE = 6,
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_PATH_NOT_FOUND = 3,
    ERROR_TOO_MANY_OPEN_FILES = 4,
    ERROR_ACCESS_DENIED = 5,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_INVALID_NAME = 123,
    ERROR_SEEK = 25,
    ERROR_NEGATIVE_SEEK = 131,
    ERROR_BROKEN_PIPE = 109,
    ERROR_DISK_FULL = 112,
    ERROR_GEN_FAILURE = 31,
    ERROR_NOACCESS = 998,
    ERROR_OPERATION_ABORTED = 995,
    ERROR_NO_DATA = 232,
    LINUX_EINTR = 4,
    LINUX_ENOENT = 2,
    LINUX_EBADF = 9,
    LINUX_EAGAIN = 11,
    LINUX_EACCES = 13,
    LINUX_EFAULT = 14,
    LINUX_ENOTDIR = 20,
    LINUX_EINVAL = 22,
    LINUX_EMFILE = 24,
    LINUX_ESPIPE = 29,
    LINUX_ENAMETOOLONG = 36,
    LINUX_ENOSPC = 28,
    LINUX_EPIPE = 32,
    LINUX_ELOOP = 40,
    HANDLE_BASE = 0x1000,
    HANDLE_STEP = 4,
    STD_HANDLE_COUNT = 3,
    FILE_HANDLE_BASE = 0x2000,
    FILE_SLOT_COUNT = 64,
    PATH_CAPACITY = 4096,
    OPEN_EXISTING = 3
};

static pe32_io_read_fn backend_read;
static pe32_io_write_fn backend_write;
static pe32_io_type_fn backend_type;
static pe32_io_open_read_fn backend_open_read;
static pe32_io_close_fn backend_close;
static pe32_io_seek_fn backend_seek;
static pe32_io_size_fn backend_size;
static pe32_u32 current_std_handles[STD_HANDLE_COUNT];
static pe32_u8 open_std_handles[STD_HANDLE_COUNT];
static char game_root[PATH_CAPACITY];
static struct {
    pe32_u32 fd;
    pe32_u32 active;
    pe32_u32 readable;
} file_slots[FILE_SLOT_COUNT];

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

static int file_slot_for_handle(pe32_u32 handle)
{
    pe32_u32 slot;
    if (handle < FILE_HANDLE_BASE ||
        handle >= FILE_HANDLE_BASE + FILE_SLOT_COUNT * HANDLE_STEP ||
        (handle - FILE_HANDLE_BASE) % HANDLE_STEP) {
        return -1;
    }
    slot = (handle - FILE_HANDLE_BASE) / HANDLE_STEP;
    return file_slots[slot].active ? (int)slot : -1;
}

static int fd_for_handle(pe32_u32 handle, pe32_u32 *fd, int *readonly)
{
    int index = index_for_handle(handle);
    if (index >= 0) {
        *fd = (pe32_u32)index;
        *readonly = 0;
        return 1;
    }
    index = file_slot_for_handle(handle);
    if (index >= 0) {
        *fd = file_slots[index].fd;
        *readonly = 1;
        return 1;
    }
    return 0;
}

/* Translate only uncomplicated relative file names. Reject drive/UNC paths,
 * parent traversal, control characters and reserved Win32 filename symbols.
 * Symlinks are rejected by the Linux openat backend, not by this parser. */
static int normalize_relative_path(const char *source, char *target)
{
    pe32_u32 length = 0;
    if (!source || !source[0] || source[0] == '/' || source[0] == '\\') {
        return 0;
    }
    while (*source) {
        const char *component;
        pe32_u32 count = 0;
        while (*source == '/' || *source == '\\') {
            ++source;
        }
        component = source;
        while (*source && *source != '/' && *source != '\\') {
            unsigned char ch = (unsigned char)*source;
            if (ch < 32 || ch == ':' || ch == '"' || ch == '<' ||
                ch == '>' || ch == '|' || ch == '*' || ch == '?') {
                return 0;
            }
            ++source;
            ++count;
        }
        if (!count) {
            break;
        }
        if (count == 1 && component[0] == '.') {
            continue;
        }
        if (count == 2 && component[0] == '.' && component[1] == '.') {
            return 0;
        }
        if (count > 255 || length + count + 2 > PATH_CAPACITY) {
            return 0;
        }
        if (length) {
            target[length++] = '/';
        }
        while (count--) {
            target[length++] = *component++;
        }
    }
    target[length] = 0;
    return length != 0;
}

static pe32_u32 win32_io_error(int linux_errno)
{
    switch (linux_errno) {
    case LINUX_ENOENT: return ERROR_FILE_NOT_FOUND;
    case LINUX_ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case LINUX_ENAMETOOLONG: return ERROR_INVALID_NAME;
    case LINUX_EACCES: case LINUX_ELOOP: return ERROR_ACCESS_DENIED;
    case LINUX_EMFILE: return ERROR_TOO_MANY_OPEN_FILES;
    case LINUX_EBADF: return ERROR_INVALID_HANDLE;
    case LINUX_EINVAL: return ERROR_INVALID_PARAMETER;
    case LINUX_ESPIPE: return ERROR_SEEK;
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

void pe32_io_set_file_backend(pe32_io_open_read_fn open_read,
                              pe32_io_close_fn close_fn,
                              pe32_io_seek_fn seek_fn,
                              pe32_io_size_fn size_fn)
{
    backend_open_read = open_read;
    backend_close = close_fn;
    backend_seek = seek_fn;
    backend_size = size_fn;
}

void pe32_io_reset(void)
{
    pe32_u32 i;
    for (i = 0; i < FILE_SLOT_COUNT; ++i) {
        if (file_slots[i].active && backend_close) {
            (void)backend_close(file_slots[i].fd);
        }
        file_slots[i].active = 0;
    }
    for (i = 0; i < STD_HANDLE_COUNT; ++i) {
        current_std_handles[i] = HANDLE_BASE + i * HANDLE_STEP;
        open_std_handles[i] = 1;
    }
    game_root[0] = 0;
}

void pe32_io_set_image_path(const char *image_path)
{
    pe32_u32 i, last_slash = 0, length = 0;
    if (!image_path) {
        return;
    }
    while (image_path[length] && length < PATH_CAPACITY) {
        if (image_path[length] == '/') {
            last_slash = length + 1;
        }
        ++length;
    }
    if (length == PATH_CAPACITY) {
        return;
    }
    if (!last_slash) {
        game_root[0] = '.';
        game_root[1] = 0;
    } else {
        for (i = 0; i < last_slash; ++i) {
            game_root[i] = image_path[i];
        }
        game_root[last_slash] = 0;
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
    pe32_u32 fd;
    int readonly;
    int type;
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_type) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return PE32_FILE_TYPE_UNKNOWN;
    }
    (void)readonly;
    type = backend_type(fd);
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
    pe32_u32 fd;
    int readonly;
    int result;
    if (read_count) {
        *read_count = 0;
    }
    if (overlapped || !read_count || (!buffer && count)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_read) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (readonly && !file_slots[file_slot_for_handle(handle)].readable) {
        pe32_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0;
    }
    if (!count) {
        return 1;
    }
    result = backend_read(fd, buffer, count);
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
    pe32_u32 fd;
    int readonly;
    int result;
    if (written_count) {
        *written_count = 0;
    }
    if (overlapped || !written_count || (!buffer && count)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_write) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (readonly) {
        pe32_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0;
    }
    if (!count) {
        return 1;
    }
    result = backend_write(fd, buffer, count);
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
    int slot;
    if (index >= 0) {
        /* Do not close Linux stdin/stdout/stderr, which the loader uses for
         * diagnostics. All aliases to this guest handle become invalid. */
        open_std_handles[index] = 0;
        return 1;
    }
    slot = file_slot_for_handle(handle);
    if (slot < 0 || !backend_close) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (backend_close(file_slots[slot].fd) < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    file_slots[slot].active = 0;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_create_file_a(
    const char *path, pe32_u32 access, pe32_u32 share_mode,
    const void *security, pe32_u32 disposition, pe32_u32 flags,
    pe32_u32 template_handle)
{
    char relative[PATH_CAPACITY];
    pe32_u32 slot;
    int fd;
    /* Read-only OPEN_EXISTING on ordinary files, with no asynchronous or
     * special-device flags. Do not let the original EXE modify its disc. */
    if (disposition != OPEN_EXISTING || (access & ~0x80120089u) ||
        share_mode & ~7u || security || template_handle ||
        (flags & ~(0x80u | 0x20u | 0x08000000u | 0x10000000u))) {
        pe32_win32_set_last_error(ERROR_ACCESS_DENIED);
        return PE32_INVALID_HANDLE_VALUE;
    }
    if (!normalize_relative_path(path, relative)) {
        pe32_win32_set_last_error(ERROR_INVALID_NAME);
        return PE32_INVALID_HANDLE_VALUE;
    }
    if (!game_root[0] || !backend_open_read || !backend_close) {
        pe32_win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return PE32_INVALID_HANDLE_VALUE;
    }
    for (slot = 0; slot < FILE_SLOT_COUNT; ++slot) {
        if (!file_slots[slot].active) {
            break;
        }
    }
    if (slot == FILE_SLOT_COUNT) {
        pe32_win32_set_last_error(ERROR_TOO_MANY_OPEN_FILES);
        return PE32_INVALID_HANDLE_VALUE;
    }
    fd = backend_open_read(game_root, relative);
    if (fd < 0) {
        pe32_win32_set_last_error(win32_io_error(-fd));
        return PE32_INVALID_HANDLE_VALUE;
    }
    file_slots[slot].fd = (pe32_u32)fd;
    file_slots[slot].active = 1;
    file_slots[slot].readable = (access & 0x80000001u) != 0;
    return FILE_HANDLE_BASE + slot * HANDLE_STEP;
}

pe32_u32 PE32_WINAPI pe32_win32_get_file_size(pe32_u32 handle, pe32_u32 *high)
{
    pe32_u32 fd, low = 0, upper = 0;
    int readonly, result;
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_size) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }
    (void)readonly;
    result = backend_size(fd, &low, &upper);
    if (result < 0) {
        pe32_win32_set_last_error(win32_io_error(-result));
        return 0xffffffffu;
    }
    if (high) {
        *high = upper;
    }
    if (low == 0xffffffffu) {
        pe32_win32_set_last_error(ERROR_SUCCESS);
    }
    return low;
}

pe32_u32 PE32_WINAPI pe32_win32_set_file_pointer(pe32_u32 handle,
                                                   pe32_u32 distance,
                                                   pe32_u32 *distance_high,
                                                   pe32_u32 method)
{
    pe32_u32 fd, low = 0, upper = 0;
    pe32_u32 previous_low = 0, previous_high = 0;
    pe32_u32 signed_high = (distance & 0x80000000u) ? 0xffffffffu : 0;
    int readonly, result;
    if (method > 2) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0xffffffffu;
    }
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_seek) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }
    if (readonly && !file_slots[file_slot_for_handle(handle)].readable) {
        pe32_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0xffffffffu;
    }
    if (distance_high) {
        signed_high = *distance_high;
    } else {
        /* Windows reports overflow rather than moving beyond 4 GiB without
         * a high-word output. Remember the position to undo such a seek. */
        result = backend_seek(fd, 0, 0, 1, &previous_low, &previous_high);
        if (result < 0) {
            pe32_win32_set_last_error(win32_io_error(-result));
            return 0xffffffffu;
        }
    }
    result = backend_seek(fd, distance, signed_high, method, &low, &upper);
    if (result < 0) {
        pe32_win32_set_last_error(result == -LINUX_EINVAL ?
                                  ERROR_NEGATIVE_SEEK : win32_io_error(-result));
        return 0xffffffffu;
    }
    if (distance_high) {
        *distance_high = upper;
    } else if (upper) {
        /* The caller did not supply storage for a 64-bit file position. */
        (void)backend_seek(fd, previous_low, previous_high, 0, &low, &upper);
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0xffffffffu;
    }
    if (low == 0xffffffffu) {
        pe32_win32_set_last_error(ERROR_SUCCESS);
    }
    return low;
}

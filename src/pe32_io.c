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
    ERROR_NO_MORE_FILES = 18,
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
    FIND_HANDLE_BASE = 0x3000,
    FIND_SLOT_COUNT = 32,
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
static pe32_io_stat_fn backend_stat;
static pe32_io_open_dir_fn backend_open_dir;
static pe32_io_next_dir_fn backend_next_dir;
static pe32_u32 current_std_handles[STD_HANDLE_COUNT];
static pe32_u8 open_std_handles[STD_HANDLE_COUNT];
static char game_root[PATH_CAPACITY];
static struct {
    pe32_u32 fd;
    pe32_u32 active;
    pe32_u32 readable;
} file_slots[FILE_SLOT_COUNT];
static struct {
    pe32_u32 fd;
    pe32_u32 active;
    char pattern[260];
} find_slots[FIND_SLOT_COUNT];

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

static int find_slot_for_handle(pe32_u32 handle)
{
    pe32_u32 slot;
    if (handle < FIND_HANDLE_BASE ||
        handle >= FIND_HANDLE_BASE + FIND_SLOT_COUNT * HANDLE_STEP ||
        (handle - FIND_HANDLE_BASE) % HANDLE_STEP) {
        return -1;
    }
    slot = (handle - FIND_HANDLE_BASE) / HANDLE_STEP;
    return find_slots[slot].active ? (int)slot : -1;
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

/* C:\ is a virtual volume rooted at the executable's directory. It is not
 * the host's C drive, and neither DOS drives nor UNC shares grant access to
 * any host paths outside that directory. */
static const char *sandbox_relative_path(const char *path)
{
    if (path && (path[0] == 'C' || path[0] == 'c') && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\')) {
        return path + 3;
    }
    return path;
}

static int path_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

/* Expand Windows paths without accessing the filesystem. The virtual C:\
 * volume has a fixed current directory at its root; the game's real Linux
 * parent directory is hidden behind that volume. The limited bootstrap does
 * not model other drives, drive current directories, UNC or device paths. */
static int build_virtual_full_path(const char *source, char *output,
                                   pe32_u32 *file_offset)
{
    pe32_u32 used = 3;
    int last_was_directory = 1;
    if (!source || !source[0]) {
        return 0;
    }
    if (path_separator(source[0]) && path_separator(source[1])) {
        return 0; /* UNC/device path: unsupported. */
    }
    if (source[1] == ':') {
        if (source[0] != 'C' && source[0] != 'c') {
            return 0;
        }
        source += 2;
    }
    output[0] = 'C';
    output[1] = ':';
    output[2] = '\\';
    output[3] = 0;
    while (*source) {
        const char *component;
        pe32_u32 count = 0, i;
        if (path_separator(*source)) {
            last_was_directory = 1;
            ++source;
            continue;
        }
        component = source;
        while (*source && !path_separator(*source)) {
            unsigned char ch = (unsigned char)*source;
            if (ch < 32 || ch == ':' || ch == '"' || ch == '<' ||
                ch == '>' || ch == '|' || count >= 255) {
                return 0;
            }
            ++source;
            ++count;
        }
        if (count == 1 && component[0] == '.') {
            last_was_directory = 1;
            continue;
        }
        if (count == 2 && component[0] == '.' && component[1] == '.') {
            if (used > 3) {
                while (used > 3 && output[used - 1] != '\\') {
                    --used;
                }
                if (used > 3) {
                    --used;
                }
                output[used] = 0;
            }
            last_was_directory = 1;
            continue;
        }
        if (count > 255 || used + count + 2 > PATH_CAPACITY) {
            return 0;
        }
        if (used > 3) {
            output[used++] = '\\';
        }
        for (i = 0; i < count; ++i) {
            output[used++] = component[i];
        }
        output[used] = 0;
        last_was_directory = 0;
    }
    if (last_was_directory && used > 3) {
        if (used + 1 >= PATH_CAPACITY) {
            return 0;
        }
        output[used++] = '\\';
        output[used] = 0;
    }
    *file_offset = 0;
    if (!last_was_directory && used > 3) {
        pe32_u32 i = used;
        while (i > 3 && output[i - 1] != '\\') {
            --i;
        }
        *file_offset = i;
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_get_full_path_name_a(
    const char *filename, pe32_u32 capacity, char *buffer, char **file_part)
{
    char full_path[PATH_CAPACITY];
    pe32_u32 length = 0, offset = 0, i;
    if (file_part) {
        *file_part = 0;
    }
    if (!game_root[0]) {
        pe32_win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    if (!build_virtual_full_path(filename, full_path, &offset)) {
        pe32_win32_set_last_error(ERROR_INVALID_NAME);
        return 0;
    }
    while (full_path[length]) {
        ++length;
    }
    if (capacity <= length) {
        return length + 1; /* Required capacity includes terminating NUL. */
    }
    if (!buffer) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    for (i = 0; i <= length; ++i) {
        buffer[i] = full_path[i];
    }
    if (file_part && offset) {
        *file_part = buffer + offset;
    }
    return length;
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

static unsigned char fold_ascii(unsigned char ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static int equal_ascii_ci(const char *left, const char *right)
{
    while (*left && *right) {
        if (fold_ascii((unsigned char)*left++) !=
            fold_ascii((unsigned char)*right++)) {
            return 0;
        }
    }
    return *left == *right;
}

static int equal_ascii(const char *left, const char *right)
{
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

/* Case-insensitive path lookup is a fallback for Linux ENOENT/ENOTDIR. Walk
 * one component at a time using the same no-follow directory backend as
 * FindFirstFileA. An ambiguous case-fold match is rejected unless one entry
 * matches the requested spelling exactly. No user-supplied path can escape
 * the executable's directory; callers normalize and reject '..' first. */
static pe32_u32 resolve_casefold_relative(const char *relative,
                                          char *resolved, int final_directory)
{
    const char *cursor = relative;
    pe32_u32 used = 0;
    if (!backend_open_dir || !backend_next_dir || !backend_close) {
        return ERROR_PATH_NOT_FOUND;
    }
    if (final_directory && relative[0] == '.' && !relative[1]) {
        resolved[0] = '.';
        resolved[1] = 0;
        return ERROR_SUCCESS;
    }
    resolved[0] = 0;
    while (*cursor) {
        char component[256], chosen[260], name[260];
        pe32_u32 count = 0, matches = 0, i;
        struct pe32_io_stat stat;
        int exact = 0, fd, result, want_dir;
        pe32_u32 error = ERROR_SUCCESS;
        while (*cursor && *cursor != '/') {
            if (count >= sizeof(component) - 1) {
                return ERROR_INVALID_NAME;
            }
            component[count++] = *cursor++;
        }
        component[count] = 0;
        want_dir = *cursor == '/' || final_directory;
        fd = backend_open_dir(game_root, used ? resolved : ".");
        if (fd < 0) {
            return win32_io_error(-fd);
        }
        for (;;) {
            result = backend_next_dir((pe32_u32)fd, name, sizeof(name), &stat);
            if (result < 0) {
                error = win32_io_error(-result);
                break;
            }
            if (!result) {
                break;
            }
            if (!equal_ascii_ci(name, component) ||
                (stat.mode & 0170000u) != (want_dir ? 0040000u : 0100000u)) {
                continue;
            }
            if (equal_ascii(name, component)) {
                for (i = 0; i < sizeof(chosen) - 1 && name[i]; ++i) {
                    chosen[i] = name[i];
                }
                chosen[i] = 0;
                exact = 1;
                break;
            }
            if (!matches) {
                for (i = 0; i < sizeof(chosen) - 1 && name[i]; ++i) {
                    chosen[i] = name[i];
                }
                chosen[i] = 0;
            }
            ++matches;
        }
        result = backend_close((pe32_u32)fd);
        if (result < 0 && error == ERROR_SUCCESS) {
            error = win32_io_error(-result);
        }
        if (error != ERROR_SUCCESS) {
            return error;
        }
        if (!exact && matches > 1) {
            return ERROR_INVALID_NAME;
        }
        if (!exact && !matches) {
            return want_dir ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
        }
        if (used) {
            if (used + 1 >= PATH_CAPACITY) {
                return ERROR_INVALID_NAME;
            }
            resolved[used++] = '/';
        }
        for (i = 0; chosen[i]; ++i) {
            if (used + 1 >= PATH_CAPACITY) {
                return ERROR_INVALID_NAME;
            }
            resolved[used++] = chosen[i];
        }
        resolved[used] = 0;
        if (*cursor == '/') {
            ++cursor;
        }
    }
    return ERROR_SUCCESS;
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

void pe32_io_set_stat_backend(pe32_io_stat_fn stat_fn)
{
    backend_stat = stat_fn;
}

void pe32_io_set_directory_backend(pe32_io_open_dir_fn open_dir,
                                   pe32_io_next_dir_fn next_dir)
{
    backend_open_dir = open_dir;
    backend_next_dir = next_dir;
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
    for (i = 0; i < FIND_SLOT_COUNT; ++i) {
        if (find_slots[i].active && backend_close) {
            (void)backend_close(find_slots[i].fd);
        }
        find_slots[i].active = 0;
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
    char resolved[PATH_CAPACITY];
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
    if (!normalize_relative_path(sandbox_relative_path(path), relative)) {
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
    if ((fd == -LINUX_ENOENT || fd == -LINUX_ENOTDIR) &&
        backend_open_dir && backend_next_dir) {
        pe32_u32 error = resolve_casefold_relative(relative, resolved, 0);
        if (error != ERROR_SUCCESS) {
            pe32_win32_set_last_error(error);
            return PE32_INVALID_HANDLE_VALUE;
        }
        fd = backend_open_read(game_root, resolved);
    }
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

static struct pe32_filetime unix_filetime(pe32_u32 seconds,
                                          pe32_u32 nanoseconds)
{
    /* FILETIME counts 100 ns intervals since 1601-01-01 UTC. The stat64
     * seconds field is signed i386 time_t (1970 epoch). Clamp dates before
     * 1601 and ignore invalid fractional seconds. */
    long long epoch_seconds = (long long)(int)seconds + 11644473600LL;
    unsigned long long ticks = epoch_seconds > 0 ?
        (unsigned long long)epoch_seconds * 10000000ULL : 0;
    struct pe32_filetime result;
    if (nanoseconds < 1000000000u && epoch_seconds > 0) {
        ticks += nanoseconds / 100u;
    }
    result.low = (pe32_u32)ticks;
    result.high = (pe32_u32)(ticks >> 32);
    return result;
}

static int stat_for_handle(pe32_u32 handle, struct pe32_io_stat *stat)
{
    pe32_u32 fd;
    int readonly, result;
    if (!fd_for_handle(handle, &fd, &readonly) || !backend_stat) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    (void)readonly;
    result = backend_stat(fd, stat);
    if (result < 0) {
        pe32_win32_set_last_error(win32_io_error(-result));
        return 0;
    }
    if ((stat->mode & 0170000u) != 0100000u) {
        /* This read-only subset deals with regular files, not devices. */
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_get_file_time(
    pe32_u32 handle, struct pe32_filetime *creation,
    struct pe32_filetime *access, struct pe32_filetime *write)
{
    struct pe32_io_stat stat;
    struct pe32_filetime modified;
    if (!stat_for_handle(handle, &stat)) {
        return 0;
    }
    modified = unix_filetime(stat.write_seconds, stat.write_nanoseconds);
    if (creation) {
        /* POSIX stat64 has no creation time; use the last write time as a
         * documented approximation, never Linux inode-change (ctime). */
        *creation = modified;
    }
    if (access) {
        *access = unix_filetime(stat.access_seconds, stat.access_nanoseconds);
    }
    if (write) {
        *write = modified;
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_get_file_information_by_handle(
    pe32_u32 handle, struct pe32_file_information *information)
{
    struct pe32_io_stat stat;
    if (!information) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!stat_for_handle(handle, &stat)) {
        return 0;
    }
    information->attributes = 0x20u; /* FILE_ATTRIBUTE_ARCHIVE */
    if (!(stat.mode & 0222u)) {
        information->attributes |= 1u; /* FILE_ATTRIBUTE_READONLY */
    }
    information->write_time = unix_filetime(stat.write_seconds,
                                            stat.write_nanoseconds);
    information->creation_time = information->write_time;
    information->access_time = unix_filetime(stat.access_seconds,
                                             stat.access_nanoseconds);
    information->volume_serial = 0; /* No virtual Windows volume yet. */
    information->size_high = stat.size_high;
    information->size_low = stat.size_low;
    information->number_of_links = stat.links;
    information->index_high = stat.inode_high;
    information->index_low = stat.inode_low;
    return 1;
}

/* Win32 filename search uses case-insensitive matching. Only * and ? are
 * implemented; DOS wildcard subtleties and short 8.3 aliases are not yet
 * emulated. Win32's *.* accepts extensionless entries too. */
static int wildcard_matches(const char *pattern, const char *name)
{
    const char *star = 0, *retry = 0;
    if (pattern[0] == '*' && pattern[1] == '.' &&
        pattern[2] == '*' && !pattern[3]) {
        return 1;
    }
    while (*name) {
        if (*pattern == '?' ||
            (*pattern && fold_ascii((unsigned char)*pattern) ==
                         fold_ascii((unsigned char)*name))) {
            ++pattern;
            ++name;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
        } else if (star) {
            pattern = star + 1;
            name = ++retry;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') {
        ++pattern;
    }
    return !*pattern;
}

static int split_find_path(const char *path, char *directory, char *pattern)
{
    pe32_u32 length = 0, last_separator = 0, i, count;
    char prefix[PATH_CAPACITY];
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\') {
        return 0;
    }
    while (path[length] && length < PATH_CAPACITY) {
        if (path[length] == '/' || path[length] == '\\') {
            last_separator = length + 1;
        }
        ++length;
    }
    if (length == PATH_CAPACITY || length == last_separator) {
        return 0;
    }
    count = length - last_separator;
    if (count > 255 || !count ||
        (count == 1 && path[last_separator] == '.') ||
        (count == 2 && path[last_separator] == '.' &&
         path[last_separator + 1] == '.')) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        unsigned char ch = (unsigned char)path[last_separator + i];
        if (ch < 32 || ch == ':' || ch == '"' || ch == '<' ||
            ch == '>' || ch == '|') {
            return 0;
        }
        pattern[i] = (char)ch;
    }
    pattern[count] = 0;
    if (!last_separator ||
        (last_separator == 2 && path[0] == '.')) {
        directory[0] = '.';
        directory[1] = 0;
        return 1;
    }
    for (i = 0; i < last_separator - 1; ++i) {
        prefix[i] = path[i];
    }
    prefix[last_separator - 1] = 0;
    return normalize_relative_path(prefix, directory);
}

static void fill_find_data(struct pe32_find_data_a *data, const char *name,
                           const struct pe32_io_stat *stat)
{
    pe32_u32 i;
    unsigned char *bytes = (unsigned char *)data;
    for (i = 0; i < sizeof(*data); ++i) {
        bytes[i] = 0;
    }
    data->attributes = (stat->mode & 0170000u) == 0040000u ? 0x10u : 0x20u;
    if (!(stat->mode & 0222u)) {
        data->attributes |= 1u;
    }
    data->size_low = stat->size_low;
    data->size_high = stat->size_high;
    data->write_time = unix_filetime(stat->write_seconds,
                                     stat->write_nanoseconds);
    data->creation_time = data->write_time;
    data->access_time = unix_filetime(stat->access_seconds,
                                      stat->access_nanoseconds);
    for (i = 0; i < sizeof(data->filename) - 1 && name[i]; ++i) {
        data->filename[i] = name[i];
    }
}

static int find_next_match(pe32_u32 slot, struct pe32_find_data_a *data,
                           pe32_u32 no_match_error)
{
    for (;;) {
        char name[260];
        struct pe32_io_stat stat;
        int result = backend_next_dir(find_slots[slot].fd, name,
                                       sizeof(name), &stat);
        if (result < 0) {
            pe32_win32_set_last_error(win32_io_error(-result));
            return 0;
        }
        if (!result) {
            pe32_win32_set_last_error(no_match_error);
            return 0;
        }
        if (name[0] && wildcard_matches(find_slots[slot].pattern, name)) {
            fill_find_data(data, name, &stat);
            return 1;
        }
    }
}

pe32_u32 PE32_WINAPI pe32_win32_find_first_file_a(
    const char *path, struct pe32_find_data_a *data)
{
    char directory[PATH_CAPACITY], pattern[260];
    char resolved[PATH_CAPACITY];
    pe32_u32 slot;
    int fd;
    if (!data) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return PE32_INVALID_HANDLE_VALUE;
    }
    if (!split_find_path(sandbox_relative_path(path), directory, pattern)) {
        pe32_win32_set_last_error(ERROR_INVALID_NAME);
        return PE32_INVALID_HANDLE_VALUE;
    }
    if (!game_root[0] || !backend_open_dir || !backend_next_dir ||
        !backend_close) {
        pe32_win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return PE32_INVALID_HANDLE_VALUE;
    }
    for (slot = 0; slot < FIND_SLOT_COUNT; ++slot) {
        if (!find_slots[slot].active) {
            break;
        }
    }
    if (slot == FIND_SLOT_COUNT) {
        pe32_win32_set_last_error(ERROR_TOO_MANY_OPEN_FILES);
        return PE32_INVALID_HANDLE_VALUE;
    }
    fd = backend_open_dir(game_root, directory);
    if (fd == -LINUX_ENOENT || fd == -LINUX_ENOTDIR) {
        pe32_u32 error = resolve_casefold_relative(directory, resolved, 1);
        if (error != ERROR_SUCCESS) {
            pe32_win32_set_last_error(error);
            return PE32_INVALID_HANDLE_VALUE;
        }
        fd = backend_open_dir(game_root, resolved);
    }
    if (fd < 0) {
        pe32_win32_set_last_error(win32_io_error(-fd));
        return PE32_INVALID_HANDLE_VALUE;
    }
    find_slots[slot].fd = (pe32_u32)fd;
    find_slots[slot].active = 1;
    for (pe32_u32 i = 0; i < sizeof(pattern); ++i) {
        find_slots[slot].pattern[i] = pattern[i];
        if (!pattern[i]) {
            break;
        }
    }
    if (!find_next_match(slot, data, ERROR_FILE_NOT_FOUND)) {
        (void)backend_close(find_slots[slot].fd);
        find_slots[slot].active = 0;
        return PE32_INVALID_HANDLE_VALUE;
    }
    return FIND_HANDLE_BASE + slot * HANDLE_STEP;
}

pe32_u32 PE32_WINAPI pe32_win32_find_next_file_a(
    pe32_u32 handle, struct pe32_find_data_a *data)
{
    int slot = find_slot_for_handle(handle);
    if (slot < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!data) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return find_next_match((pe32_u32)slot, data, ERROR_NO_MORE_FILES);
}

pe32_u32 PE32_WINAPI pe32_win32_find_close(pe32_u32 handle)
{
    int slot = find_slot_for_handle(handle);
    int result;
    if (slot < 0 || !backend_close) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    result = backend_close(find_slots[slot].fd);
    if (result < 0) {
        pe32_win32_set_last_error(win32_io_error(-result));
        return 0;
    }
    find_slots[slot].active = 0;
    return 1;
}

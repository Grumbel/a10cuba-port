/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Freestanding Linux/i386 PE32 loader bootstrap.
 *
 * Maps/copies original x86 machine code, applies HIGHLOW base relocations,
 * resolves imports between supplied PE modules and can enter synthetic test
 * images. It refuses to execute images with unresolved imports or a PE TLS
 * directory. Only a small single-thread Kernel32 subset (DLL lookup, dynamic
 * TLS, errors and private heaps) is implemented. This is NOT a Windows
 * process implementation: no PEB, TEB, SEH, CRT or COM startup yet.
 */

#include "pe32_link.h"
#include "pe32_win32.h"
#include "pe32_heap.h"
#include "pe32_dsound.h"
#include "pe32_proc.h"
#include "pe32_winmm.h"
#include "pe32_io.h"
#include "pe32_stubs.h"
#include "pe32_user.h"
#include "pe32_wnd.h"
#include "pe32_ddraw.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

static u32 read32(const u8 *data);

enum {
    SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5,
    SYS_CLOSE = 6, SYS_LSEEK = 19, SYS_OLD_MMAP = 90, SYS_NANOSLEEP = 162, SYS_CLOCK_GETTIME = 265, SYS_EXIT_GROUP = 252,
    SYS_MUNMAP = 91, SYS_SET_THREAD_AREA = 243, SYS_LLSEEK = 140, SYS_FSTAT64 = 197,
    SYS_OPENAT = 295, SYS_GETDENTS64 = 220,
    O_DIRECTORY = 00200000, O_NOFOLLOW = 00400000,
    O_CLOEXEC = 02000000, O_NONBLOCK = 00004000,
    PROT_READ = 1, PROT_WRITE = 2, PROT_EXEC = 4,
    MAP_PRIVATE = 2, MAP_ANONYMOUS = 0x20, MAP_FIXED_NOREPLACE = 0x100000,
    MAX_FILE_SIZE = 64 * 1024 * 1024,
    MAX_IMAGE_SIZE = 256 * 1024 * 1024
};

static s32 syscall1(u32 number, u32 a)
{
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(number), "b"(a) : "memory");
    return result;
}

static s32 syscall3(u32 number, u32 a, u32 b, u32 c)
{
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

static s32 syscall2(u32 number, u32 a, u32 b)
{
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(number), "b"(a), "c"(b) : "memory");
    return result;
}

static s32 syscall4(u32 number, u32 a, u32 b, u32 c, u32 d)
{
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return result;
}

static s32 syscall5(u32 number, u32 a, u32 b, u32 c, u32 d, u32 e)
{
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(number), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return result;
}

/* Linux i386's old_mmap syscall accepts one pointer to six 32-bit words. */
static void *map_memory(u32 address, u32 length, u32 protection, u32 flags,
                        s32 fd)
{
    u32 args[6] = { address, length, protection, flags, (u32)fd, 0 };
    u32 result = (u32)syscall1(SYS_OLD_MMAP, (u32)args);
    return result >= (u32)-4095 ? (void *)0 : (void *)result;
}

static void *map_heap_pages(u32 size)
{
    return map_memory(0, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1);
}

static void unmap_heap_pages(void *memory, u32 size)
{
    (void)syscall2(SYS_MUNMAP, (u32)memory, size);
}

static void exit_process(u32 code)
{
    for (;;) {
        (void)syscall1(SYS_EXIT_GROUP, code);
    }
}

static void sleep_milliseconds(u32 milliseconds)
{
    u32 request[2] = { milliseconds / 1000, (milliseconds % 1000) * 1000000u };
    (void)syscall2(SYS_NANOSLEEP, (u32)request, 0);
}

static u32 monotonic_milliseconds(void)
{
    u32 ts[2]; /* i386 timespec: 32-bit seconds and nanoseconds */
    if (syscall2(SYS_CLOCK_GETTIME, 1, (u32)ts) < 0) {
        return 0;
    }
    return ts[0] * 1000u + ts[1] / 1000000u;
}

static s32 read_standard_fd(u32 fd, void *buffer, u32 count)
{
    return syscall3(SYS_READ, fd, (u32)buffer, count);
}

static s32 write_standard_fd(u32 fd, const void *buffer, u32 count)
{
    return syscall3(SYS_WRITE, fd, (u32)buffer, count);
}

static u32 string_length(const char *str)
{
    u32 n = 0;
    while (str[n]) {
        ++n;
    }
    return n;
}

static int strings_equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void print(const char *str)
{
    syscall3(SYS_WRITE, 1, (u32)str, string_length(str));
}

static void print_error(const char *str)
{
    syscall3(SYS_WRITE, 2, (u32)str, string_length(str));
}

static void print_hex(u32 value)
{
    static const char digits[] = "0123456789abcdef";
    char out[11] = "0x00000000";
    u32 i;
    for (i = 0; i < 8; ++i) {
        out[9 - i] = digits[value & 15];
        value >>= 4;
    }
    syscall3(SYS_WRITE, 1, (u32)out, 10);
}

static void print_decimal(u32 value)
{
    char out[11];
    u32 i = 11;
    do {
        out[--i] = '0' + (char)(value % 10);
        value /= 10;
    } while (value);
    syscall3(SYS_WRITE, 1, (u32)(out + i), 11 - i);
}

static int has_range(u32 length, u32 offset, u32 size)
{
    return offset <= length && size <= length - offset;
}

static u16 read16(const u8 *data)
{
    return (u16)(data[0] | ((u16)data[1] << 8));
}

static u32 read32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static s32 type_standard_fd(u32 fd)
{
    /* Linux i386 fstat64 writes its stable kernel stat64 structure. Per
     * asm/stat.h, st_mode is the little-endian u32 at offset 16. 128 bytes
     * suffice for the 96-byte structure without linking 32-bit libc. */
    u8 stat_bytes[128];
    s32 result = syscall2(SYS_FSTAT64, fd, (u32)stat_bytes);
    u32 mode;
    if (result < 0) {
        return result;
    }
    mode = read32(stat_bytes + 16) & 0170000u;
    if (mode == 0020000u) { /* S_IFCHR */
        return PE32_FILE_TYPE_CHAR;
    }
    if (mode == 0010000u || mode == 0140000u) { /* FIFO or socket */
        return PE32_FILE_TYPE_PIPE;
    }
    if (mode == 0100000u || mode == 0040000u || mode == 0060000u) {
        /* Regular file, directory, block device. */
        return PE32_FILE_TYPE_DISK;
    }
    return PE32_FILE_TYPE_UNKNOWN;
}

static s32 open_game_path(const char *root, const char *relative,
                          int directory)
{
    /* Reject symlinks for every component. A game path cannot escape its
     * executable directory, even when a directory entry is a symlink.
     * Exact filename case is required until Win32 case folding is ported. */
    s32 fd = syscall3(SYS_OPEN, (u32)root,
                      O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC, 0);
    if (fd < 0) {
        return fd;
    }
    if (directory && relative[0] == '.' && !relative[1]) {
        return fd;
    }
    while (*relative) {
        char component[256];
        u32 count = 0, flags = O_NOFOLLOW | O_CLOEXEC;
        s32 next;
        while (*relative && *relative != '/') {
            if (count >= sizeof(component) - 1) {
                (void)syscall1(SYS_CLOSE, (u32)fd);
                return -36; /* ENAMETOOLONG */
            }
            component[count++] = *relative++;
        }
        component[count] = 0;
        if (*relative == '/') {
            flags |= O_DIRECTORY;
            ++relative;
        } else if (directory) {
            flags |= O_DIRECTORY;
        }
        next = syscall4(SYS_OPENAT, (u32)fd, (u32)component, flags, 0);
        (void)syscall1(SYS_CLOSE, (u32)fd);
        if (next < 0) {
            return next;
        }
        fd = next;
    }
    {
        u8 stat_bytes[128];
        s32 result = syscall2(SYS_FSTAT64, (u32)fd, (u32)stat_bytes);
        u32 desired = directory ? 0040000u : 0100000u;
        if (result < 0 || (read32(stat_bytes + 16) & 0170000u) != desired) {
            (void)syscall1(SYS_CLOSE, (u32)fd);
            return result < 0 ? result : -21; /* EISDIR/nonregular */
        }
    }
    return fd;
}

static s32 open_game_read(const char *root, const char *relative)
{
    return open_game_path(root, relative, 0);
}

static s32 open_game_directory(const char *root, const char *relative)
{
    return open_game_path(root, relative, 1);
}

static s32 close_game_fd(u32 fd)
{
    return syscall1(SYS_CLOSE, fd);
}

static s32 seek_game_fd(u32 fd, u32 low, u32 high, u32 method,
                        u32 *result_low, u32 *result_high)
{
    u32 position[2] = {0, 0};
    s32 result = syscall5(SYS_LLSEEK, fd, high, low, (u32)position, method);
    if (result >= 0) {
        *result_low = position[0];
        *result_high = position[1];
    }
    return result;
}

static s32 size_game_fd(u32 fd, u32 *low, u32 *high)
{
    u8 stat_bytes[128];
    s32 result = syscall2(SYS_FSTAT64, fd, (u32)stat_bytes);
    if (result >= 0) {
        /* Linux/i386 stat64: st_size starts at byte offset 44. */
        *low = read32(stat_bytes + 44);
        *high = read32(stat_bytes + 48);
    }
    return result;
}

static s32 stat_game_fd(u32 fd, struct pe32_io_stat *stat)
{
    u8 bytes[128];
    s32 result = syscall2(SYS_FSTAT64, fd, (u32)bytes);
    if (result < 0) {
        return result;
    }
    /* Linux/i386 stat64 in asm/stat.h: mode@16, nlink@20, size@44,
     * atime@64, mtime@72 and 64-bit inode@88. Time fields are seconds
     * and nanoseconds, not Windows FILETIME values. */
    stat->mode = read32(bytes + 16);
    stat->links = read32(bytes + 20);
    stat->size_low = read32(bytes + 44);
    stat->size_high = read32(bytes + 48);
    stat->inode_low = read32(bytes + 88);
    stat->inode_high = read32(bytes + 92);
    stat->access_seconds = read32(bytes + 64);
    stat->access_nanoseconds = read32(bytes + 68);
    stat->write_seconds = read32(bytes + 72);
    stat->write_nanoseconds = read32(bytes + 76);
    return 0;
}

static s32 next_game_directory(u32 directory_fd, char *name, u32 capacity,
                               struct pe32_io_stat *stat)
{
    /* Linux getdents64 advances to the end of the returned batch. On each
     * successful entry, seek back to that entry's d_off (its successor) so
     * the next call can resume without retaining a buffer across calls. */
    u8 records[1024];
    for (;;) {
        s32 length = syscall3(SYS_GETDENTS64, directory_fd,
                              (u32)records, sizeof(records));
        u32 offset = 0;
        if (length <= 0) {
            return length;
        }
        while (offset < (u32)length) {
            const u8 *record = records + offset;
            u32 record_length, filename_length = 0;
            s32 fd, result;
            if ((u32)length - offset < 20) {
                return -22; /* Invalid kernel dirent: EINVAL. */
            }
            record_length = read16(record + 16);
            if (record_length < 20 || record_length > (u32)length - offset) {
                return -22;
            }
            while (19 + filename_length < record_length &&
                   record[19 + filename_length]) {
                ++filename_length;
            }
            offset += record_length;
            if (19 + filename_length == record_length || !filename_length ||
                filename_length >= capacity ||
                (filename_length == 1 && record[19] == '.') ||
                (filename_length == 2 && record[19] == '.' && record[20] == '.')) {
                continue;
            }
            /* Skip known non-file types; the fstat64 check handles DT_UNKNOWN. */
            if (record[18] && record[18] != 4 && record[18] != 8) {
                continue;
            }
            for (u32 i = 0; i < filename_length; ++i) {
                name[i] = (char)record[19 + i];
            }
            name[filename_length] = 0;
            fd = syscall4(SYS_OPENAT, directory_fd, (u32)name,
                          O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0);
            if (fd < 0) {
                continue; /* Unreadable entry or symlink: never follow it. */
            }
            result = stat_game_fd((u32)fd, stat);
            (void)syscall1(SYS_CLOSE, (u32)fd);
            if (result < 0 ||
                ((stat->mode & 0170000u) != 0100000u &&
                 (stat->mode & 0170000u) != 0040000u)) {
                continue;
            }
            {
                u32 position[2];
                result = syscall5(SYS_LLSEEK, directory_fd,
                                  read32(record + 12), read32(record + 8),
                                  (u32)position, 0);
            }
            return result < 0 ? result : 1;
        }
    }
}

static void write32(u8 *data, u32 value)
{
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
    data[2] = (u8)(value >> 16);
    data[3] = (u8)(value >> 24);
}

static void copy_bytes(u8 *dst, const u8 *src, u32 size)
{
    u32 i;
    for (i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
}

static const char *bounded_string(const u8 *image, u32 size, u32 rva)
{
    u32 i;
    if (rva >= size) {
        return (const char *)0;
    }
    for (i = rva; i < size; ++i) {
        if (!image[i]) {
            return (const char *)(image + rva);
        }
    }
    return (const char *)0;
}

static int apply_relocations(u8 *image, u32 size, u32 rva, u32 length,
                             u32 delta)
{
    u32 pos = 0;
    if (!rva || !length || !has_range(size, rva, length)) {
        print_error("PE32: relocated image has no valid relocation table\n");
        return 0;
    }
    while (pos < length) {
        u32 page, block_size, i;
        if (!has_range(length, pos, 8)) {
            return 0;
        }
        page = read32(image + rva + pos);
        block_size = read32(image + rva + pos + 4);
        if (block_size < 8 || block_size % 2 ||
            !has_range(length, pos, block_size)) {
            return 0;
        }
        for (i = 8; i < block_size; i += 2) {
            u16 item = read16(image + rva + pos + i);
            u32 type = item >> 12;
            u32 offset = item & 0xfff;
            u32 target;
            if (type == 0) { /* IMAGE_REL_BASED_ABSOLUTE: padding */
                continue;
            }
            if (type != 3 || page > size || offset > size - page) {
                print_error("PE32: unsupported or out-of-range base relocation\n");
                return 0;
            }
            target = page + offset;
            if (!has_range(size, target, 4)) {
                return 0;
            }
            write32(image + target, read32(image + target) + delta);
        }
        pos += block_size;
    }
    return 1;
}

static int inspect_imports(const u8 *image, u32 size, u32 rva, u32 length)
{
    u32 pos = 0, count = 0;
    if (!rva && !length) {
        print("Imports: none\n");
        return 0;
    }
    if (!rva || !length || !has_range(size, rva, length)) {
        return -1;
    }
    while (has_range(length, pos, 20)) {
        const u8 *entry = image + rva + pos;
        u32 original_thunk = read32(entry);
        u32 timestamp = read32(entry + 4);
        u32 forwarder = read32(entry + 8);
        u32 name_rva = read32(entry + 12);
        u32 first_thunk = read32(entry + 16);
        const char *name;
        if (!original_thunk && !timestamp && !forwarder && !name_rva &&
            !first_thunk) {
            print("Import DLLs: ");
            print_decimal(count);
            print("\n");
            return (int)count;
        }
        name = bounded_string(image, size, name_rva);
        if (!name || ++count > 128) {
            return -1;
        }
        print("  import: ");
        print(name);
        print("\n");
        pos += 20;
    }
    return -1; /* Missing terminating import descriptor. */
}

static int load_image(const u8 *file, u32 length, const char *path,
                      struct pe32_module *module)
{
    u32 pe, coff, opt, section_table, sections, opt_size;
    u32 preferred, image_size, headers_size, entry, import_rva, import_size;
    u32 reloc_rva, reloc_size, tls_rva, tls_size, base, i;
    u8 *image;
    int imported;

    if (!has_range(length, 0, 0x40) || read16(file) != 0x5a4d) {
        print_error("PE32: missing MZ header\n");
        return 1;
    }
    pe = read32(file + 0x3c);
    if (!has_range(length, pe, 24) || read32(file + pe) != 0x00004550) {
        print_error("PE32: invalid PE signature\n");
        return 1;
    }
    coff = pe + 4;
    sections = read16(file + coff + 2);
    opt_size = read16(file + coff + 16);
    opt = pe + 24;
    if (read16(file + coff) != 0x014c || !sections || sections > 96 ||
        opt_size < 96 || !has_range(length, opt, opt_size) ||
        read16(file + opt) != 0x010b) {
        print_error("PE32: expected an i386 PE32 image\n");
        return 1;
    }
    section_table = opt + opt_size;
    if (!has_range(length, section_table, sections * 40)) {
        print_error("PE32: truncated section table\n");
        return 1;
    }
    entry = read32(file + opt + 16);
    preferred = read32(file + opt + 28);
    image_size = read32(file + opt + 56);
    headers_size = read32(file + opt + 60);
    if (!preferred || preferred % 4096 || !image_size ||
        image_size > MAX_IMAGE_SIZE || !has_range(image_size, 0, headers_size) ||
        !has_range(length, 0, headers_size) ||
        !has_range(image_size, entry, 1)) {
        print_error("PE32: invalid image base, sizes or entry RVA\n");
        return 1;
    }
    import_rva = import_size = reloc_rva = reloc_size = tls_rva = tls_size = 0;
    if (read32(file + opt + 92) >= 2 && opt_size >= 96 + 2 * 8) {
        import_rva = read32(file + opt + 96 + 8);
        import_size = read32(file + opt + 96 + 12);
    }
    if (read32(file + opt + 92) >= 6 && opt_size >= 96 + 6 * 8) {
        reloc_rva = read32(file + opt + 96 + 5 * 8);
        reloc_size = read32(file + opt + 100 + 5 * 8);
    }
    if (read32(file + opt + 92) >= 10 && opt_size >= 96 + 10 * 8) {
        tls_rva = read32(file + opt + 96 + 9 * 8);
        tls_size = read32(file + opt + 100 + 9 * 8);
    }

    image = map_memory(preferred, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1);
    if (!image) {
        image = map_memory(0, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1);
    }
    if (!image) {
        print_error("PE32: could not map image memory\n");
        return 1;
    }
    copy_bytes(image, file, headers_size);
    for (i = 0; i < sections; ++i) {
        const u8 *s = file + section_table + i * 40;
        u32 virtual_size = read32(s + 8);
        u32 rva = read32(s + 12);
        u32 raw_size = read32(s + 16);
        u32 raw_pos = read32(s + 20);
        u32 memory_size = virtual_size > raw_size ? virtual_size : raw_size;
        if (!has_range(image_size, rva, memory_size) ||
            !has_range(length, raw_pos, raw_size)) {
            print_error("PE32: section extends beyond file or image\n");
            return 1;
        }
        copy_bytes(image + rva, file + raw_pos, raw_size);
    }
    base = (u32)image;
    if (base != preferred &&
        !apply_relocations(image, image_size, reloc_rva, reloc_size,
                           base - preferred)) {
        print_error("PE32: image relocation failed\n");
        return 1;
    }
    print("PE32 i386: preferred=");
    print_hex(preferred);
    print(" mapped=");
    print_hex(base);
    print(" image_size=");
    print_decimal(image_size);
    print(" entry=");
    print_hex(base + entry);
    print("\n");
    imported = inspect_imports(image, image_size, import_rva, import_size);
    if (imported < 0) {
        print_error("PE32: invalid import directory\n");
        return 1;
    }
    if (tls_rva || tls_size) {
        print("TLS directory present; initialization is not implemented\n");
    }
    module->image = image;
    module->size = image_size;
    module->base = base;
    module->entry_rva = entry;
    module->export_rva = module->export_size = 0;
    if (read32(file + opt + 92) >= 1 && opt_size >= 96 + 1 * 8) {
        module->export_rva = read32(file + opt + 96);
        module->export_size = read32(file + opt + 100);
    }
    module->import_rva = import_rva;
    module->import_size = import_size;
    module->tls_rva = tls_rva;
    module->tls_size = tls_size;
    module->path = path;
    if (module->export_rva || module->export_size) {
        const char *name = pe32_export_module_name(module);
        if (!name) {
            print_error("PE32: invalid export directory/name\n");
            return 1;
        }
        print("  export module: ");
        print(name);
        print("\n");
    }
    return 0;
}

static int load_file(const char *path, struct pe32_module *module)
{
    s32 fd, end;
    const u8 *file;
    int status;
    fd = syscall3(SYS_OPEN, (u32)path, 0, 0);
    if (fd < 0) {
        print_error("PE32: cannot open image\n");
        return 1;
    }
    end = syscall3(SYS_LSEEK, (u32)fd, 0, 2);
    if (end < 0 || end > MAX_FILE_SIZE || end < 64) {
        print_error("PE32: invalid/oversized image file\n");
        syscall1(SYS_CLOSE, (u32)fd);
        return 1;
    }
    file = (const u8 *)map_memory(0, (u32)end, PROT_READ, MAP_PRIVATE, fd);
    syscall1(SYS_CLOSE, (u32)fd);
    if (!file) {
        print_error("PE32: could not map image file\n");
        return 1;
    }
    status = load_image(file, (u32)end, path, module);
    return status;
}

static void link_report(void *context, const char *dll, const char *name,
                        u32 ordinal, enum pe32_link_status status, u32 address)
{
    (void)context;
    if (status == PE32_LINK_OK) {
        print("  linked ");
    } else {
        print_error("PE32: missing or unsupported import (see stdout)\n");
        print("  unresolved ");
    }
    print(dll);
    print("!");
    if (name) {
        print(name);
    } else {
        print("#");
        print_decimal(ordinal);
    }
    if (status == PE32_LINK_OK) {
        print(" -> ");
        print_hex(address);
    } else if (status == PE32_LINK_FORWARDER) {
        print(" (forwarded export not implemented)");
    } else if (status == PE32_LINK_INVALID) {
        print(" (invalid PE export)");
    }
    print("\n");
}

/* Windows code addresses its thread block through fs: (SEH chain at fs:0,
 * stack bounds, self pointer, PEB). Give the guest a minimal one. */
static u32 fake_teb[1024] __attribute__((aligned(4096)));
static u32 fake_peb[256] __attribute__((aligned(16)));

static int install_teb(void)
{
    struct {
        u32 entry_number, base_addr, limit, flags;
    } desc;
    u32 stack_top;
    u32 selector;
    int result;
    __asm__ volatile("mov %%esp, %0" : "=r"(stack_top));
    fake_teb[0] = 0xffffffffu;                 /* end of SEH chain */
    fake_teb[1] = (stack_top + 0xfffu) & ~0xfffu;   /* stack base */
    fake_teb[2] = stack_top - 0x100000u;       /* stack limit */
    fake_teb[6] = (u32)fake_teb;               /* NT_TIB.Self */
    fake_teb[12] = (u32)fake_peb;              /* TEB.ProcessEnvironmentBlock */
    desc.entry_number = 0xffffffffu;
    desc.base_addr = (u32)fake_teb;
    desc.limit = sizeof fake_teb - 1;
    desc.flags = 1;                            /* seg_32bit, byte granular */
    result = (int)syscall3(SYS_SET_THREAD_AREA, (u32)&desc, 0, 0);
    if (result < 0) {
        return result;
    }
    selector = desc.entry_number * 8 + 3;
    __asm__ volatile("mov %0, %%fs" : : "r"((unsigned short)selector));
    return 0;
}

/* Headless output: write the primary surface as a binary PPM every so often. */
static const char *dump_frame_path;
static u32 present_count;

static void write_all(s32 fd, const void *data, u32 size)
{
    const char *p = data;
    while (size) {
        s32 n = syscall3(SYS_WRITE, (u32)fd, (u32)p, size);
        if (n <= 0) {
            return;
        }
        p += n;
        size -= (u32)n;
    }
}

static void dump_frame(const struct pe32_ddraw_frame *frame)
{
    static unsigned char row[4096 * 3];
    char header[32];
    u32 x, y, n = 0, v;
    s32 fd;
    if (!dump_frame_path || (present_count++ % 20) != 0 || frame->width > 4096) {
        return;
    }
    fd = syscall3(SYS_OPEN, (u32)dump_frame_path, 0x241, 0644);
    if (fd < 0) {
        return;
    }
    header[n++] = 'P'; header[n++] = '6'; header[n++] = '\n';
    for (v = frame->width, x = 1000000; x; x /= 10) {
        if (v / x || n > 3) { header[n++] = (char)('0' + v / x % 10); }
    }
    header[n++] = ' ';
    for (v = frame->height, x = 1000000, y = n; x; x /= 10) {
        if (v / x || n > y) { header[n++] = (char)('0' + v / x % 10); }
    }
    header[n++] = '\n'; header[n++] = '2'; header[n++] = '5'; header[n++] = '5';
    header[n++] = '\n';
    write_all(fd, header, n);
    for (y = 0; y < frame->height; y++) {
        for (x = 0; x < frame->width; x++) {
            u32 idx = frame->pixels[y * frame->pitch + x];
            const unsigned char *c = frame->palette ? frame->palette + idx * 4
                                                    : (const unsigned char *)0;
            row[x * 3] = c ? c[0] : (unsigned char)idx;
            row[x * 3 + 1] = c ? c[1] : (unsigned char)idx;
            row[x * 3 + 2] = c ? c[2] : (unsigned char)idx;
        }
        write_all(fd, row, frame->width * 3);
    }
    syscall3(SYS_CLOSE, (u32)fd, 0, 0);
}

/* SDL front end link: frames go out on frame_fd as {magic, width, height,
 * palette[256*4], pixels[width*height]}; input records {type, a, b, 0} arrive
 * on the non-blocking input_fd. */
static s32 frame_fd = -1, input_fd = -1, audio_fd = -1;
static u32 audio_last_ms, audio_carry;
static s32 ui_result;
static int ui_ready;

static void host_input_poll(void)
{
    u32 rec[4 * 16];
    s32 n, i;
    if (input_fd < 0) {
        return;
    }
    while ((n = syscall3(SYS_READ, (u32)input_fd, (u32)rec, sizeof rec)) >= 16) {
        for (i = 0; i + 16 <= n; i += 16) {
            if (rec[i / 4] == 7) {          /* native screen result */
                ui_result = (s32)rec[i / 4 + 2];
                ui_ready = 1;
                continue;
            }
            pe32_user_input_event(rec[i / 4], rec[i / 4 + 1], rec[i / 4 + 2]);
        }
    }
}

/* Mix DirectSound buffers for the time elapsed since the last frame and hand
 * the PCM (stereo S16, 44100 Hz) to the host. The fd is non-blocking: if the
 * host falls behind, the chunk is dropped rather than stalling the game. */
static void audio_pump(void)
{
    static short chunk[8192 * 2];
    u32 now = monotonic_milliseconds(), elapsed, owed, frames;
    elapsed = audio_last_ms ? now - audio_last_ms : 0;
    audio_last_ms = now;
    if (elapsed > 200) {
        elapsed = 200;
    }
    owed = elapsed * (PE32_DSOUND_RATE / 100) + audio_carry;
    frames = owed / 10;
    audio_carry = owed % 10;
    if (frames > 8192) {
        frames = 8192;
    }
    if (!frames) {
        return;
    }
    pe32_dsound_mix(chunk, frames);
    if (audio_fd >= 0) {
        syscall3(SYS_WRITE, (u32)audio_fd, (u32)chunk, frames * 4);
    }
}

/* Ask the host to run a native screen and wait for its answer. */
static int host_ui(int screen, int arg)
{
    u32 header[3];
    if (frame_fd < 0 || input_fd < 0) {
        return -2;
    }
    header[0] = 0xa10f0002u;
    header[1] = (u32)screen;
    header[2] = (u32)arg;
    write_all(frame_fd, header, sizeof header);
    ui_ready = 0;
    while (!ui_ready) {
        host_input_poll();
        if (!ui_ready) {
            pe32_win32_sleep(10);
        }
    }
    return (int)ui_result;
}

static void host_present(const struct pe32_ddraw_frame *frame)
{
    u32 header[3], y;
    static unsigned char palette[1024];
    header[0] = 0xa10f0001u;
    header[1] = frame->width;
    header[2] = frame->height;
    for (y = 0; y < 1024; y++) {
        palette[y] = frame->palette ? frame->palette[y] : (unsigned char)(y / 4);
    }
    write_all(frame_fd, header, sizeof header);
    write_all(frame_fd, palette, sizeof palette);
    for (y = 0; y < frame->height; y++) {
        write_all(frame_fd, frame->pixels + y * frame->pitch, frame->width);
    }
    audio_pump();
    host_input_poll();
}

static u32 parse_number(const char *s)
{
    u32 v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (u32)(*s++ - '0');
    }
    return v;
}

static void stub_report(const char *dll, const char *name)
{
    print_error("stub called: ");
    print_error(dll);
    print_error("!");
    print_error(name);
    print_error("\n");
}

int pe32_main(int argc, char **argv)
{
    struct pe32_module modules[2] = { {0}, {0} };
    int module_count = 0, link = 0, execute = 0, i;
    pe32_heap_set_backend(map_heap_pages, unmap_heap_pages);
    pe32_winmm_set_clock(monotonic_milliseconds);
    pe32_proc_set_backend(exit_process, sleep_milliseconds);
    pe32_io_set_backend(read_standard_fd, write_standard_fd, type_standard_fd);
    pe32_io_set_file_backend(open_game_read, close_game_fd,
                             seek_game_fd, size_game_fd);
    pe32_io_set_stat_backend(stat_game_fd);
    pe32_io_set_directory_backend(open_game_directory, next_game_directory);
    if (argc < 2 || argc > 16) {
        print_error("Usage: a10-pe32-native IMAGE [DLL] [--link] [--run-entry] [--auto-accept] [--dump-frame PPM] [--frame-fd N] [--input-fd N] [--audio-fd N] [--mission N]\n");
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        if (strings_equal(argv[i], "--run-entry")) {
            execute = 1;
            link = 1;
        } else if (strings_equal(argv[i], "--auto-accept")) {
            pe32_wnd_set_auto_accept(1);
        } else if (strings_equal(argv[i], "--dump-frame") && i + 1 < argc) {
            dump_frame_path = argv[++i];
            pe32_ddraw_set_present(dump_frame);
        } else if (strings_equal(argv[i], "--frame-fd") && i + 1 < argc) {
            frame_fd = (s32)parse_number(argv[++i]);
            pe32_ddraw_set_present(host_present);
        } else if (strings_equal(argv[i], "--audio-fd") && i + 1 < argc) {
            audio_fd = (s32)parse_number(argv[++i]);
        } else if (strings_equal(argv[i], "--mission") && i + 1 < argc) {
            pe32_wnd_set_mission((int)parse_number(argv[++i]) - 1);
        } else if (strings_equal(argv[i], "--input-fd") && i + 1 < argc) {
            input_fd = (s32)parse_number(argv[++i]);
            pe32_user_set_input_poll(host_input_poll);
            pe32_wnd_set_ui(host_ui);
        } else if (strings_equal(argv[i], "--link")) {
            link = 1;
        } else if (argv[i][0] == '-' || module_count == 2) {
            print_error("PE32: unknown option or too many modules\n");
            return 2;
        } else {
            if (load_file(argv[i], &modules[module_count]) != 0) {
                return 1;
            }
            ++module_count;
        }
    }
    if (!module_count) {
        print_error("PE32: missing IMAGE\n");
        return 2;
    }
    if (link) {
        pe32_stubs_set_logger(stub_report);
        pe32_win32_initialize(modules, (u32)module_count);
        for (i = 0; i < module_count; ++i) {
            enum pe32_link_status status;
            u32 linked_count = 0;
            print("Binding ");
            print(modules[i].path);
            print("...\n");
            status = pe32_bind_imports_with_host(
                &modules[i], modules, (u32)module_count,
                link_report, (void *)0, &linked_count,
                pe32_win32_resolve_import, (void *)0);
            if (status != PE32_LINK_OK) {
                print_error("PE32: module imports not fully resolved\n");
                return 1;
            }
            print("  linked functions: ");
            print_decimal(linked_count);
            print("\n");
        }
    }
    if (execute) {
        host_input_poll(); /* learn about gamepads before the game reads its prefs */
        int (*entry_point)(void);
        int result;
        for (i = 0; i < module_count; ++i) {
            if (modules[i].tls_rva || modules[i].tls_size) {
                print_error("PE32: cannot execute modules with TLS yet\n");
                return 1;
            }
        }
        if (install_teb() < 0) {
            print_error("PE32: cannot install thread block\n");
            return 1;
        }
        for (i = 1; i < module_count; ++i) {
            /* DLL_PROCESS_ATTACH: Windows runs this inside LoadLibrary. */
            if (modules[i].entry_rva) {
                int (__attribute__((stdcall)) *dll_main)(u32, u32, u32) =
                    (int (__attribute__((stdcall)) *)(u32, u32, u32))(
                        modules[i].image + modules[i].entry_rva);
                print("Attaching DLL...\n");
                dll_main(modules[i].base, 1, 0);
            }
        }
        print("Executing x86 entry point...\n");
        entry_point = (int (*)(void))(modules[0].image + modules[0].entry_rva);
        result = entry_point();
        print("Entry returned ");
        print_decimal((u32)result);
        print("\n");
    }
    return 0;
}

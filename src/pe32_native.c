/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Freestanding Linux/i386 PE32 loader bootstrap.
 *
 * Maps/copies original x86 machine code, applies HIGHLOW base relocations,
 * resolves imports between supplied PE modules and can enter synthetic test
 * images. This is NOT a Windows process implementation: no PEB, TEB, SEH,
 * TLS, Win32 startup, COM or imported Windows OS APIs yet. It refuses to
 * execute images with unresolved imports or TLS.
 */

#include "pe32_link.h"
#include "pe32_win32.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

enum {
    SYS_WRITE = 4, SYS_OPEN = 5,
    SYS_CLOSE = 6, SYS_LSEEK = 19, SYS_OLD_MMAP = 90,
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

/* Linux i386's old_mmap syscall accepts one pointer to six 32-bit words. */
static void *map_memory(u32 address, u32 length, u32 protection, u32 flags,
                        s32 fd)
{
    u32 args[6] = { address, length, protection, flags, (u32)fd, 0 };
    u32 result = (u32)syscall1(SYS_OLD_MMAP, (u32)args);
    return result >= (u32)-4095 ? (void *)0 : (void *)result;
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

int pe32_main(int argc, char **argv)
{
    struct pe32_module modules[2] = { {0}, {0} };
    int module_count = 0, link = 0, execute = 0, i;
    if (argc < 2 || argc > 5) {
        print_error("Usage: a10-pe32-native IMAGE [DLL] [--link] [--run-entry]\n");
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        if (strings_equal(argv[i], "--run-entry")) {
            execute = 1;
            link = 1;
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
        int (*entry_point)(void);
        int result;
        for (i = 0; i < module_count; ++i) {
            if (modules[i].tls_rva || modules[i].tls_size) {
                print_error("PE32: cannot execute modules with TLS yet\n");
                return 1;
            }
        }
        print("Executing raw x86 entry point (no Windows environment)...\n");
        entry_point = (int (*)(void))(modules[0].image + modules[0].entry_rva);
        result = entry_point();
        print("Entry returned ");
        print_decimal((u32)result);
        print("\n");
    }
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Host-native tests of the exact resolver built into the Linux/i386 loader. */
#include "pe32_link.h"
#include "pe32_win32.h"
#include "pe32_heap.h"
#include "pe32_io.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned mapped_regions;
static unsigned unmapped_regions;

static void *host_heap_map(pe32_u32 size)
{
    void *memory = malloc(size);
    if (memory) {
        ++mapped_regions;
    }
    return memory;
}

static void host_heap_unmap(void *memory, pe32_u32 size)
{
    (void)size;
    ++unmapped_regions;
    free(memory);
}

static pe32_u8 dll_image[0x1000];
static pe32_u8 exe_image[0x1000];

static struct pe32_module dll = {
    dll_image, sizeof(dll_image), 0x10000000, 0x600,
    0x100, 0x100, 0, 0, 0, 0, "C:\\game\\A10SIM.DLL"
};
static struct pe32_module exe = {
    exe_image, sizeof(exe_image), 0x00400000, 0x600,
    0, 0, 0x100, 40, 0, 0, "test.exe"
};

static void put16(pe32_u8 *buf, pe32_u32 at, pe32_u32 value)
{
    buf[at] = (pe32_u8)value;
    buf[at + 1] = (pe32_u8)(value >> 8);
}

static void put32(pe32_u8 *buf, pe32_u32 at, pe32_u32 value)
{
    buf[at] = (pe32_u8)value;
    buf[at + 1] = (pe32_u8)(value >> 8);
    buf[at + 2] = (pe32_u8)(value >> 16);
    buf[at + 3] = (pe32_u8)(value >> 24);
}

static pe32_u32 get32(const pe32_u8 *buf, pe32_u32 at)
{
    return (pe32_u32)buf[at] | ((pe32_u32)buf[at + 1] << 8) |
           ((pe32_u32)buf[at + 2] << 16) | ((pe32_u32)buf[at + 3] << 24);
}

static void reset_fixtures(void)
{
    memset(dll_image, 0, sizeof(dll_image));
    memset(exe_image, 0, sizeof(exe_image));

    /* Export directory: base ordinal 7, two functions, two names. */
    put32(dll_image, 0x100 + 12, 0x1c0);
    put32(dll_image, 0x100 + 16, 7);
    put32(dll_image, 0x100 + 20, 2);
    put32(dll_image, 0x100 + 24, 2);
    put32(dll_image, 0x100 + 28, 0x140);
    put32(dll_image, 0x100 + 32, 0x160);
    put32(dll_image, 0x100 + 36, 0x180);
    put32(dll_image, 0x140, 0x600);
    put32(dll_image, 0x144, 0x700);
    put32(dll_image, 0x160, 0x1d0);
    put32(dll_image, 0x164, 0x1e0);
    put16(dll_image, 0x180, 0);
    put16(dll_image, 0x182, 1);
    memcpy(dll_image + 0x1c0, "A10SIM.DLL", 11);
    memcpy(dll_image + 0x1d0, "_SIM_Init@4", 12);
    memcpy(dll_image + 0x1e0, "_SIM_Start@4", 13);

    /* Import descriptor (followed by a null descriptor), lookup and IAT. */
    put32(exe_image, 0x100, 0x200);
    put32(exe_image, 0x10c, 0x300);
    put32(exe_image, 0x110, 0x220);
    memcpy(exe_image + 0x300, "a10sim.dll", 11);
    put32(exe_image, 0x200, 0x400);
    put16(exe_image, 0x400, 1); /* Hint is advisory. */
    memcpy(exe_image + 0x402, "_SIM_Init@4", 12);
    put32(exe_image, 0x220, 0x400);
}

static void test_named_and_ordinal(void)
{
    pe32_u32 address = 0, count = 0;
    reset_fixtures();
    assert(strcmp(pe32_export_module_name(&dll), "A10SIM.DLL") == 0);
    assert(pe32_find_export(&dll, "_SIM_Init@4", 0, &address) == PE32_LINK_OK);
    assert(address == 0x10000600);
    assert(pe32_find_export(&dll, "_SIM_init@4", 0, &address) == PE32_LINK_MISSING);
    assert(pe32_find_export(&dll, 0, 8, &address) == PE32_LINK_OK);
    assert(address == 0x10000700);
    assert(pe32_find_export(&dll, 0, 6, &address) == PE32_LINK_MISSING);
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_OK);
    assert(count == 1);
    assert(get32(exe_image, 0x220) == 0x10000600);

    reset_fixtures();
    put32(exe_image, 0x200, 0x80000008);
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_OK);
    assert(get32(exe_image, 0x220) == 0x10000700);
}

static void test_fallback_lookup_and_name(void)
{
    pe32_u32 count;
    reset_fixtures();
    /* Name match via path, even with a missing export table. */
    dll.export_rva = dll.export_size = 0;
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_MISSING);
    dll.export_rva = dll.export_size = 0x100;

    /* No OriginalFirstThunk: read lookup directly from the IAT. */
    put32(exe_image, 0x100, 0);
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_OK);
    assert(get32(exe_image, 0x220) == 0x10000600);
}

static void test_no_partial_bind_on_missing(void)
{
    pe32_u32 count;
    reset_fixtures();
    /* First import is valid, second is absent; neither IAT slot is changed. */
    put32(exe_image, 0x204, 0x420);
    put32(exe_image, 0x224, 0x420);
    memcpy(exe_image + 0x422, "_SIM_Absent@4", 14);
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_MISSING);
    assert(count == 0);
    assert(get32(exe_image, 0x220) == 0x400);
    assert(get32(exe_image, 0x224) == 0x420);

    reset_fixtures();
    assert(pe32_bind_imports(&exe, 0, 0, 0, 0, &count) == PE32_LINK_MISSING);
    assert(get32(exe_image, 0x220) == 0x400);
}

static void test_reject_bad_images_and_forwarders(void)
{
    pe32_u32 address, count;
    reset_fixtures();
    /* Export address points into export directory: forwarded exports. */
    put32(dll_image, 0x140, 0x1f0);
    memcpy(dll_image + 0x1f0, "kernel32.Sleep", 15);
    assert(pe32_find_export(&dll, "_SIM_Init@4", 0, &address) == PE32_LINK_FORWARDER);
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_FORWARDER);
    assert(get32(exe_image, 0x220) == 0x400);

    reset_fixtures();
    put32(dll_image, 0x100 + 28, 0xfffffffc);
    assert(pe32_find_export(&dll, "_SIM_Init@4", 0, &address) == PE32_LINK_INVALID);

    reset_fixtures();
    put32(exe_image, 0x100, 0xfff); /* Bad lookup RVA. */
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_INVALID);

    reset_fixtures();
    put32(exe_image, 0x200, 0x80010007); /* Invalid ordinal flags. */
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_INVALID);

    reset_fixtures();
    put32(exe_image, 0x114, 1); /* No terminating import descriptor. */
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_INVALID);

    reset_fixtures();
    exe.import_size = 19;
    assert(pe32_bind_imports(&exe, &dll, 1, 0, 0, &count) == PE32_LINK_INVALID);
    exe.import_size = 40;
}

static enum pe32_link_status host_resolver(void *context, const char *dll,
                                          const char *name, pe32_u32 ordinal,
                                          pe32_u32 *address)
{
    (void)context;
    (void)ordinal;
    if (strcmp(dll, "KERNEL32.dll") == 0 &&
        name && strcmp(name, "Sleep") == 0) {
        *address = 0x12345678;
        return PE32_LINK_OK;
    }
    return PE32_LINK_MISSING;
}

static void test_explicit_host_resolver(void)
{
    pe32_u32 count = 0;
    reset_fixtures();
    memcpy(exe_image + 0x300, "KERNEL32.dll", 13);
    memcpy(exe_image + 0x402, "Sleep", 6);
    assert(pe32_bind_imports_with_host(&exe, &dll, 1, 0, 0, &count,
                                       host_resolver, 0) == PE32_LINK_OK);
    assert(count == 1);
    assert(get32(exe_image, 0x220) == 0x12345678);

    reset_fixtures();
    memcpy(exe_image + 0x300, "KERNEL32.dll", 13);
    memcpy(exe_image + 0x402, "Unknown", 8);
    assert(pe32_bind_imports_with_host(&exe, &dll, 1, 0, 0, &count,
                                       host_resolver, 0) == PE32_LINK_MISSING);
    assert(count == 0);
    assert(get32(exe_image, 0x220) == 0x400);
}

static void test_preloaded_win32_module_lookups(void)
{
    struct pe32_module modules[] = {exe, dll};
    pe32_u32 handle;
    reset_fixtures();
    pe32_win32_initialize(modules, 2);
    assert(pe32_win32_get_module_handle_a(0) == 0x00400000);
    assert(pe32_win32_get_module_handle_a("a10sim.dll") == 0x10000000);
    assert(pe32_win32_get_module_handle_a("C:\\GAME\\A10SIM.DLL") == 0x10000000);
    assert(pe32_win32_get_module_handle_a("D:\\GAME\\A10SIM.DLL") == 0);
    assert(pe32_win32_get_module_handle_a("A10CUBA.EXE") == 0);
    handle = pe32_win32_load_library_a("A10Sim.DLL");
    assert(handle == 0x10000000);
    assert(pe32_win32_load_library_a("missing.dll") == 0);
    assert(pe32_win32_load_library_a(0) == 0);
    assert(pe32_win32_get_proc_address(handle, "_SIM_Init@4") == 0x10000600);
    assert(pe32_win32_get_proc_address(handle, (const char *)(unsigned long)8)
           == 0x10000700);
    assert(pe32_win32_get_proc_address(handle, "_sim_Init@4") == 0);
    assert(pe32_win32_get_proc_address(handle, "missing") == 0);
    assert(pe32_win32_get_proc_address(handle, 0) == 0);
    assert(pe32_win32_get_proc_address(0x1234, "_SIM_Init@4") == 0);

    /* Do not return the address of a forwarded export as code. */
    put32(dll_image, 0x140, 0x1f0);
    memcpy(dll_image + 0x1f0, "kernel32.Sleep", 15);
    assert(pe32_win32_get_proc_address(handle, "_SIM_Init@4") == 0);
    pe32_win32_initialize(0, 0);
    assert(pe32_win32_get_module_handle_a(0) == 0);
    assert(pe32_win32_load_library_a("A10Sim.dll") == 0);
}

static void test_win32_tls_and_last_error(void)
{
    pe32_u32 first, second, i;
    pe32_win32_initialize(0, 0);
    assert(pe32_win32_get_last_error() == 0);
    pe32_win32_set_last_error(0xabcdef12);
    assert(pe32_win32_get_last_error() == 0xabcdef12);

    first = pe32_win32_tls_alloc();
    second = pe32_win32_tls_alloc();
    assert(first == 0 && second == 1);
    assert(pe32_win32_get_last_error() == 0xabcdef12);
    assert(pe32_win32_tls_get_value(first) == 0);
    assert(pe32_win32_get_last_error() == 0); /* Success with NULL. */
    assert(pe32_win32_tls_set_value(first, 0xdeadbeef) == 1);
    assert(pe32_win32_tls_set_value(second, 0x12345678) == 1);
    pe32_win32_set_last_error(5);
    assert(pe32_win32_tls_get_value(first) == 0xdeadbeef);
    assert(pe32_win32_get_last_error() == 0);
    assert(pe32_win32_tls_get_value(second) == 0x12345678);

    assert(pe32_win32_tls_free(first) == 1);
    assert(pe32_win32_tls_get_value(first) == 0);
    assert(pe32_win32_get_last_error() == 87); /* Invalid index. */
    /* Windows allows TlsSetValue on an unallocated low index. */
    assert(pe32_win32_tls_set_value(first, 1) == 1);
    assert(pe32_win32_tls_free(first) == 0);
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_tls_alloc() == first); /* Reuse and reset slot. */
    assert(pe32_win32_tls_get_value(first) == 0);
    assert(pe32_win32_tls_set_value(64, 1) == 0);
    assert(pe32_win32_get_last_error() == 87);

    for (i = 2; i < 64; ++i) {
        assert(pe32_win32_tls_alloc() == i);
    }
    assert(pe32_win32_tls_alloc() == PE32_TLS_OUT_OF_INDEXES);
    assert(pe32_win32_get_last_error() == 8); /* Allocation exhausted. */
    assert(pe32_win32_tls_free(0xffffffffu) == 0);
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_tls_get_value(64) == 0);
    assert(pe32_win32_get_last_error() == 87);

    pe32_win32_initialize(0, 0);
    assert(pe32_win32_tls_alloc() == 0);
    assert(pe32_win32_get_last_error() == 0);
}

static void test_win32_process_startup_subset(void)
{
    struct pe32_module modules[] = {exe, dll};
    char path[64];
    unsigned char startup[PE32_STARTUPINFOA_SIZE + 4];
    const char *line;
    char *environment;
    pe32_u16 *wide_environment;
    unsigned i;

    modules[0].path = "C:\\Program Files\\A10 Cuba\\A10Cuba.exe";
    pe32_win32_initialize(modules, 2);
    line = pe32_win32_get_command_line_a();
    assert(strcmp(line, "\"C:\\Program Files\\A10 Cuba\\A10Cuba.exe\"") == 0);
    assert(pe32_win32_get_current_process() == 0xffffffffu);
    assert(pe32_win32_get_current_thread() == 0xfffffffeu);
    assert(pe32_win32_get_current_thread_id() == 1);

    pe32_win32_set_last_error(0x1234);
    memset(path, 0xee, sizeof(path));
    assert(pe32_win32_get_module_file_name_a(0, path, sizeof(path)) ==
           strlen(modules[0].path));
    assert(strcmp(path, modules[0].path) == 0);
    assert(pe32_win32_get_last_error() == 0x1234);
    assert(pe32_win32_get_module_file_name_a(dll.base, path, sizeof(path)) ==
           strlen(dll.path));
    assert(strcmp(path, dll.path) == 0);
    assert(pe32_win32_get_module_file_name_a(dll.base, path, 4) == 4);
    assert(memcmp(path, "C:\\", 3) == 0 && path[3] == 0);
    assert(pe32_win32_get_last_error() == 122);
    assert(pe32_win32_get_module_file_name_a(0, path, 1) == 1);
    assert(path[0] == 0);
    assert(pe32_win32_get_module_file_name_a(0, path, 0) == 0);
    assert(pe32_win32_get_last_error() == 122);
    assert(pe32_win32_get_module_file_name_a(0x1234, path, sizeof(path)) == 0);
    assert(pe32_win32_get_last_error() == 126);

    memset(startup, 0xaa, sizeof(startup));
    pe32_win32_get_startup_info_a(startup);
    assert(startup[0] == PE32_STARTUPINFOA_SIZE);
    for (i = 1; i < PE32_STARTUPINFOA_SIZE; ++i) {
        assert(startup[i] == 0);
    }
    for (i = PE32_STARTUPINFOA_SIZE; i < sizeof(startup); ++i) {
        assert(startup[i] == 0xaa);
    }

    environment = pe32_win32_get_environment_strings();
    wide_environment = pe32_win32_get_environment_strings_w();
    assert(environment && environment[0] == 0 && environment[1] == 0);
    assert(wide_environment && !wide_environment[0] && !wide_environment[1]);
    assert(pe32_win32_free_environment_strings_a(environment));
    assert(pe32_win32_free_environment_strings_w(wide_environment));
    assert(!pe32_win32_free_environment_strings_a((char *)wide_environment));
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_free_environment_strings_w((pe32_u16 *)environment));
    assert(pe32_win32_get_last_error() == 87);

    pe32_win32_initialize(0, 0);
    assert(pe32_win32_get_command_line_a()[0] == 0);
    assert(pe32_win32_get_module_file_name_a(0, path, sizeof(path)) == 0);
    assert(pe32_win32_get_last_error() == 126);
}

static void test_win32_strings_and_interlocked(void)
{
    char buffer[24] = {0};
    volatile pe32_u32 counter = 0;

    pe32_win32_initialize(0, 0);
    pe32_win32_set_last_error(0x12345678);
    assert(pe32_win32_lstrlen_a(0) == 0);
    assert(pe32_win32_lstrlen_a("") == 0);
    assert(pe32_win32_lstrlen_a("A\xc3\xb1") == 3); /* Byte count. */

    assert(pe32_win32_lstrcpy_a(buffer, "Cuba") == buffer);
    assert(!strcmp(buffer, "Cuba"));
    assert(pe32_win32_lstrcat_a(buffer, "!") == buffer);
    assert(!strcmp(buffer, "Cuba!"));
    assert(pe32_win32_lstrcat_a(buffer, "") == buffer);
    assert(!strcmp(buffer, "Cuba!"));

    memset(buffer, 'X', sizeof(buffer));
    assert(pe32_win32_lstrcpyn_a(buffer, "Longer", 5) == buffer);
    assert(!strcmp(buffer, "Long"));
    assert(buffer[5] == 'X');
    assert(pe32_win32_lstrcpyn_a(buffer, "Hi", 8) == buffer);
    assert(!strcmp(buffer, "Hi"));
    assert(pe32_win32_lstrcpyn_a(buffer, "Hi", 1) == buffer);
    assert(buffer[0] == 0);
    buffer[0] = 'X';
    assert(pe32_win32_lstrcpyn_a(buffer, "Hi", 0) == 0);
    assert(buffer[0] == 'X');
    assert(pe32_win32_lstrcpyn_a(buffer, "Hi", -1) == 0);
    assert(buffer[0] == 'X');

    assert(pe32_win32_interlocked_increment(&counter) == 1);
    assert(counter == 1);
    assert(pe32_win32_interlocked_decrement(&counter) == 0);
    assert(pe32_win32_interlocked_decrement(&counter) == 0xffffffffu);
    assert(counter == 0xffffffffu);
    assert(pe32_win32_interlocked_increment(&counter) == 0);
    counter = 0x7fffffffu;
    assert(pe32_win32_interlocked_increment(&counter) == 0x80000000u);
    assert(pe32_win32_interlocked_decrement(&counter) == 0x7fffffffu);
    assert(pe32_win32_get_last_error() == 0x12345678);
}

static void test_win32_single_thread_critical_sections(void)
{
    struct pe32_critical_section section;
    struct pe32_critical_section second;

    assert(sizeof(section) == 24);
    memset(&section, 0x55, sizeof(section));
    pe32_win32_initialize(0, 0);
    pe32_win32_set_last_error(0xface);

    pe32_win32_initialize_critical_section(&section);
    assert(section.debug_info == 0);
    assert(section.lock_count == 0xffffffffu);
    assert(section.recursion_count == 0);
    assert(section.owning_thread == 0);
    assert(section.lock_semaphore == 0);
    assert(section.spin_count == 0);

    pe32_win32_enter_critical_section(&section);
    assert(section.lock_count == 0);
    assert(section.recursion_count == 1);
    assert(section.owning_thread == 1);
    pe32_win32_enter_critical_section(&section);
    assert(section.lock_count == 1);
    assert(section.recursion_count == 2);

    pe32_win32_initialize_critical_section(&second);
    pe32_win32_enter_critical_section(&second);
    pe32_win32_leave_critical_section(&section);
    assert(section.lock_count == 0);
    assert(section.recursion_count == 1);
    assert(section.owning_thread == 1);
    pe32_win32_leave_critical_section(&second);
    assert(second.lock_count == 0xffffffffu);
    assert(second.owning_thread == 0);
    pe32_win32_leave_critical_section(&section);
    assert(section.lock_count == 0xffffffffu);
    assert(section.recursion_count == 0);
    assert(section.owning_thread == 0);

    pe32_win32_leave_critical_section(&section); /* Invalid: no corruption. */
    assert(section.lock_count == 0xffffffffu);
    pe32_win32_delete_critical_section(&section);
    pe32_win32_delete_critical_section(&second);
    assert(section.lock_count == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 0xface);
}

static const char standard_input_data[] = "control-input";
static char standard_output_data[32];
static char standard_error_data[32];
static pe32_u32 standard_input_offset;
static pe32_u32 standard_output_offset;
static pe32_u32 standard_error_offset;

static const char game_file_data[] = "Native A-10 file bytes";
static struct {
    int active;
    int big;
    int64_t offset;
} fake_game_files[70];
static pe32_u32 fake_game_opens;
static pe32_u32 fake_game_closes;
static struct {
    int active;
    int directory_kind;
    unsigned cursor;
} fake_directories[40];
static unsigned fake_dir_opens;
static unsigned fake_dir_closes;

static const struct {
    int directory_kind;
    const char *name;
    pe32_u32 mode;
} directory_entries[] = {
    {1, "Gear.wav", 0100444},
    {1, "TAUNT2.WAV", 0100644},
    {1, "README", 0100644},
    {1, "Notes.txt", 0100644},
    {1, "SubFolder", 0040755},
    {2, "D-DropZone04.dmo", 0100644},
    {2, "B-DropZone04.dmo", 0100644},
    {0, "A10Cuba.exe", 0100644},
    {0, "Media", 0040755},
    {0, "Missions", 0040755},
};

static int fake_open_directory(const char *root, const char *relative)
{
    int kind;
    unsigned index = fake_dir_opens;
    assert(!strcmp(root, "/opt/games/A-10Cuba/"));
    if (!strcmp(relative, "Media")) {
        kind = 1;
    } else if (!strcmp(relative, "Missions")) {
        kind = 2;
    } else if (!strcmp(relative, ".")) {
        kind = 0;
    } else {
        return -2;
    }
    assert(index < sizeof(fake_directories) / sizeof(fake_directories[0]));
    ++fake_dir_opens;
    fake_directories[index].active = 1;
    fake_directories[index].directory_kind = kind;
    fake_directories[index].cursor = 0;
    return (int)(200 + index);
}

static int fake_next_directory(pe32_u32 fd, char *name,
                               pe32_u32 capacity, struct pe32_io_stat *stat)
{
    unsigned index = fd - 200;
    if (fd < 200 || index >= fake_dir_opens || !fake_directories[index].active) {
        return -9;
    }
    while (fake_directories[index].cursor <
           sizeof(directory_entries) / sizeof(directory_entries[0])) {
        unsigned i = fake_directories[index].cursor++;
        if (directory_entries[i].directory_kind !=
            fake_directories[index].directory_kind) {
            continue;
        }
        assert(strlen(directory_entries[i].name) + 1 <= capacity);
        strcpy(name, directory_entries[i].name);
        memset(stat, 0, sizeof(*stat));
        stat->mode = directory_entries[i].mode;
        stat->links = 1;
        stat->size_low = (pe32_u32)(strlen(name) + 100);
        stat->write_seconds = 0;
        stat->write_nanoseconds = 123456789;
        return 1;
    }
    return 0;
}

static int fake_game_open(const char *root, const char *relative)
{
    pe32_u32 index = fake_game_opens;
    assert(!strcmp(root, "/opt/games/A-10Cuba/"));
    if (!strcmp(relative, "missing.dat")) {
        return -2;
    }
    if (!strcmp(relative, "locked.dat")) {
        return -13;
    }
    if (strcmp(relative, "Media/Gear.wav") && strcmp(relative, "Big.dat")) {
        return -2;
    }
    assert(index < sizeof(fake_game_files) / sizeof(fake_game_files[0]));
    ++fake_game_opens;
    fake_game_files[index].active = 1;
    fake_game_files[index].big = !strcmp(relative, "Big.dat");
    fake_game_files[index].offset = 0;
    return (int)(10 + index);
}

static int fake_game_close(pe32_u32 fd)
{
    if (fd >= 200) {
        unsigned index = fd - 200;
        if (index >= fake_dir_opens || !fake_directories[index].active) {
            return -9;
        }
        fake_directories[index].active = 0;
        ++fake_dir_closes;
        return 0;
    }
    pe32_u32 index = fd - 10;
    if (fd < 10 || index >= fake_game_opens || !fake_game_files[index].active) {
        return -9;
    }
    fake_game_files[index].active = 0;
    ++fake_game_closes;
    return 0;
}

static int fake_game_seek(pe32_u32 fd, pe32_u32 low, pe32_u32 high,
                          pe32_u32 method, pe32_u32 *result_low,
                          pe32_u32 *result_high)
{
    pe32_u32 index = fd - 10;
    int64_t base, delta, position;
    if (fd < 10 || index >= fake_game_opens || !fake_game_files[index].active) {
        return -9;
    }
    base = method == 0 ? 0 : (method == 1 ? fake_game_files[index].offset :
          (fake_game_files[index].big ? (int64_t)0x1ffffffffLL :
           (int64_t)(sizeof(game_file_data) - 1)));
    delta = (int64_t)(int32_t)high * 4294967296LL + low;
    position = base + delta;
    if (position < 0) {
        return -22;
    }
    fake_game_files[index].offset = position;
    *result_low = (pe32_u32)(uint64_t)position;
    *result_high = (pe32_u32)((uint64_t)position >> 32);
    return 0;
}

static int fake_game_size(pe32_u32 fd, pe32_u32 *low, pe32_u32 *high)
{
    pe32_u32 index = fd - 10;
    if (fd < 10 || index >= fake_game_opens || !fake_game_files[index].active) {
        return -9;
    }
    *low = fake_game_files[index].big ? 0xffffffffu :
           (pe32_u32)(sizeof(game_file_data) - 1);
    *high = fake_game_files[index].big ? 1 : 0;
    return 0;
}

static int fake_game_stat(pe32_u32 fd, struct pe32_io_stat *stat)
{
    pe32_u32 index = fd - 10;
    if (fd < 10 || index >= fake_game_opens || !fake_game_files[index].active) {
        return -9;
    }
    memset(stat, 0, sizeof(*stat));
    stat->mode = 0100444u; /* Regular file, no write permission. */
    stat->links = 3;
    stat->size_low = fake_game_files[index].big ? 0xffffffffu :
                     (pe32_u32)(sizeof(game_file_data) - 1);
    stat->size_high = fake_game_files[index].big ? 1 : 0;
    stat->inode_low = 0x9abcde00u;
    stat->inode_high = 0x12345678u;
    stat->access_seconds = 86400; /* 1970-01-02 00:00 UTC */
    stat->access_nanoseconds = 500;
    stat->write_seconds = 0; /* 1970-01-01 00:00 UTC */
    stat->write_nanoseconds = 123456789;
    return 0;
}

static int fake_standard_read(pe32_u32 fd, void *buffer, pe32_u32 count)
{
    pe32_u32 remaining;
    if (fd >= 10 && fd - 10 < fake_game_opens &&
        fake_game_files[fd - 10].active) {
        int64_t offset = fake_game_files[fd - 10].offset;
        if (offset >= (int64_t)(sizeof(game_file_data) - 1)) {
            return 0;
        }
        remaining = (pe32_u32)(sizeof(game_file_data) - 1 - offset);
        if (count > remaining) {
            count = remaining;
        }
        memcpy(buffer, game_file_data + offset, count);
        fake_game_files[fd - 10].offset += count;
        return (int)count;
    }
    if (fd != 0) {
        return -9; /* EBADF */
    }
    remaining = (pe32_u32)sizeof(standard_input_data) - 1 -
                standard_input_offset;
    if (count > remaining) {
        count = remaining;
    }
    memcpy(buffer, standard_input_data + standard_input_offset, count);
    standard_input_offset += count;
    return (int)count;
}

static int fake_standard_write(pe32_u32 fd, const void *buffer, pe32_u32 count)
{
    char *output;
    pe32_u32 *offset;
    if (count && ((const char *)buffer)[0] == '!') {
        return -32; /* EPIPE */
    }
    if (fd == 1) {
        output = standard_output_data;
        offset = &standard_output_offset;
    } else if (fd == 2) {
        output = standard_error_data;
        offset = &standard_error_offset;
    } else {
        return -9;
    }
    if (count > 32 - *offset) {
        count = 32 - *offset;
    }
    memcpy(output + *offset, buffer, count);
    *offset += count;
    return (int)count;
}

static int fake_standard_type(pe32_u32 fd)
{
    if (fd >= 10 && fd - 10 < fake_game_opens &&
        fake_game_files[fd - 10].active) {
        return PE32_FILE_TYPE_DISK;
    }
    if (fd == 0) {
        return PE32_FILE_TYPE_PIPE;
    }
    if (fd == 1) {
        return PE32_FILE_TYPE_DISK;
    }
    return fd == 2 ? (int)PE32_FILE_TYPE_CHAR : -9;
}

static void test_win32_standard_io(void)
{
    pe32_u32 in, out, err, transferred;
    char input[32] = {0};
    standard_input_offset = standard_output_offset = standard_error_offset = 0;
    pe32_io_set_backend(fake_standard_read, fake_standard_write,
                        fake_standard_type);
    pe32_win32_initialize(0, 0);
    pe32_win32_set_last_error(0xbeef);
    in = pe32_win32_get_std_handle(PE32_STD_INPUT_HANDLE);
    out = pe32_win32_get_std_handle(PE32_STD_OUTPUT_HANDLE);
    err = pe32_win32_get_std_handle(PE32_STD_ERROR_HANDLE);
    assert(in && out && err && in != out && out != err);
    assert(pe32_win32_get_file_type(in) == PE32_FILE_TYPE_PIPE);
    assert(pe32_win32_get_file_type(out) == PE32_FILE_TYPE_DISK);
    assert(pe32_win32_get_file_type(err) == PE32_FILE_TYPE_CHAR);
    assert(pe32_win32_set_handle_count(128) == 128);
    assert(pe32_win32_get_last_error() == 0xbeef);

    assert(pe32_win32_read_file(in, input, 7, &transferred, 0));
    assert(transferred == 7 && !memcmp(input, "control", 7));
    assert(pe32_win32_read_file(in, input + 7, 25, &transferred, 0));
    assert(transferred == 6 && !strcmp(input, "control-input"));
    assert(pe32_win32_read_file(in, input, 8, &transferred, 0));
    assert(transferred == 0); /* EOF is success. */
    assert(pe32_win32_read_file(in, 0, 0, &transferred, 0));
    assert(transferred == 0);
    assert(!pe32_win32_read_file(in, input, 1, 0, 0));
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_read_file(in, input, 1, &transferred, input));
    assert(pe32_win32_get_last_error() == 87 && transferred == 0);

    assert(pe32_win32_write_file(out, "hello", 5, &transferred, 0));
    assert(transferred == 5 && !memcmp(standard_output_data, "hello", 5));
    assert(pe32_win32_set_std_handle(PE32_STD_OUTPUT_HANDLE, err));
    assert(pe32_win32_get_std_handle(PE32_STD_OUTPUT_HANDLE) == err);
    assert(pe32_win32_write_file(err, "error", 5, &transferred, 0));
    assert(transferred == 5 && !memcmp(standard_error_data, "error", 5));
    assert(!pe32_win32_write_file(err, "!", 1, &transferred, 0));
    assert(transferred == 0 && pe32_win32_get_last_error() == 109);
    assert(!pe32_win32_write_file(in, "no", 2, &transferred, 0));
    assert(transferred == 0 && pe32_win32_get_last_error() == 6);

    assert(pe32_win32_get_std_handle(42) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_set_std_handle(42, out));
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_set_std_handle(PE32_STD_INPUT_HANDLE, 0xdeadbeef));
    assert(pe32_win32_get_file_type(0xdeadbeef) == PE32_FILE_TYPE_UNKNOWN);
    assert(pe32_win32_get_last_error() == 6);

    assert(pe32_win32_close_handle(err));
    assert(!pe32_win32_close_handle(err));
    assert(pe32_win32_get_last_error() == 6);
    assert(!pe32_win32_write_file(
        pe32_win32_get_std_handle(PE32_STD_OUTPUT_HANDLE), "x", 1,
        &transferred, 0));
    assert(pe32_win32_get_last_error() == 6);
    pe32_win32_initialize(0, 0);
    assert(pe32_win32_get_file_type(err) == PE32_FILE_TYPE_CHAR);
    pe32_io_set_backend(0, 0, 0);
    assert(pe32_win32_get_file_type(err) == PE32_FILE_TYPE_UNKNOWN);
    assert(pe32_win32_get_last_error() == 6);
}

static void test_win32_readonly_game_files(void)
{
    struct pe32_module modules[] = {exe, dll};
    struct pe32_file_information information;
    struct pe32_filetime creation, access, write;
    const uint64_t unix_epoch = 116444736000000000ULL;
    const uint64_t write_ticks = unix_epoch + 1234567;
    const uint64_t access_ticks = unix_epoch + 86400ULL * 10000000ULL + 5;
    pe32_u32 first, second, big, metadata, high, count, position, handles[64];
    unsigned i;
    char data[sizeof(game_file_data)] = {0};

    fake_game_opens = fake_game_closes = 0;
    memset(fake_game_files, 0, sizeof(fake_game_files));
    modules[0].path = "/opt/games/A-10Cuba/A10Cuba.exe";
    pe32_io_set_backend(fake_standard_read, fake_standard_write,
                        fake_standard_type);
    pe32_io_set_file_backend(fake_game_open, fake_game_close,
                             fake_game_seek, fake_game_size);
    pe32_io_set_stat_backend(fake_game_stat);
    pe32_win32_initialize(modules, 2);
    assert(sizeof(struct pe32_filetime) == 8);
    assert(sizeof(struct pe32_file_information) == 52);

    assert(pe32_win32_create_file_a("../Media/Gear.wav", 0x80000000u, 1,
                                    0, 3, 0x80, 0) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 123);
    assert(pe32_win32_create_file_a("/etc/passwd", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("C:\\Windows\\win.ini", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("Media\\..\\Gear.wav", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("Media/Gear.wav:stream", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("Media/Gear.wav", 0x40000000u, 1,
                                    0, 3, 0, 0) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 5); /* Write access denied. */
    assert(pe32_win32_create_file_a("Media/Gear.wav", 0, 1,
                                    0, 2, 0, 0) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("Media/Gear.wav", 0, 1,
                                    0, 3, 0x40000000u, 0) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("Media/Gear.wav", 0, 1,
                                    (void *)1, 3, 0, 0) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_create_file_a("missing.dat", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 2);
    assert(pe32_win32_create_file_a("locked.dat", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 5);
    assert(!fake_game_opens);

    first = pe32_win32_create_file_a(".\\Media\\Gear.wav", 0x80000000u, 1,
                                     0, 3, 0x80, 0);
    second = pe32_win32_create_file_a("Media//Gear.wav", 0x80000000u, 3,
                                      0, 3, 0, 0);
    assert(first != PE32_INVALID_HANDLE_VALUE &&
           second != PE32_INVALID_HANDLE_VALUE && first != second);
    assert(pe32_win32_get_file_type(first) == PE32_FILE_TYPE_DISK);
    assert(pe32_win32_get_file_time(first, &creation, &access, &write));
    assert(write.low == (pe32_u32)write_ticks);
    assert(write.high == (pe32_u32)(write_ticks >> 32));
    assert(creation.low == write.low && creation.high == write.high);
    assert(access.low == (pe32_u32)access_ticks);
    assert(access.high == (pe32_u32)(access_ticks >> 32));
    assert(pe32_win32_get_file_time(first, 0, 0, 0));
    memset(&information, 0xcd, sizeof(information));
    assert(pe32_win32_get_file_information_by_handle(first, &information));
    assert(information.attributes == 0x21);
    assert(information.creation_time.low == creation.low);
    assert(information.write_time.high == write.high);
    assert(information.access_time.low == access.low);
    assert(information.volume_serial == 0);
    assert(information.size_high == 0);
    assert(information.size_low == sizeof(game_file_data) - 1);
    assert(information.number_of_links == 3);
    assert(information.index_high == 0x12345678u);
    assert(information.index_low == 0x9abcde00u);
    assert(!pe32_win32_get_file_information_by_handle(first, 0));
    assert(pe32_win32_get_last_error() == 87);
    high = 99;
    assert(pe32_win32_get_file_size(first, &high) == sizeof(game_file_data) - 1);
    assert(high == 0);
    assert(pe32_win32_get_file_size(first, 0) == sizeof(game_file_data) - 1);
    assert(pe32_win32_read_file(first, data, 6, &count, 0));
    assert(count == 6 && !memcmp(data, game_file_data, 6));
    assert(pe32_win32_read_file(second, data, 6, &count, 0));
    assert(count == 6 && !memcmp(data, game_file_data, 6));
    assert(pe32_win32_set_file_pointer(first, 2, 0, 1) == 8);
    assert(pe32_win32_read_file(first, data, 4, &count, 0));
    assert(count == 4 && !memcmp(data, game_file_data + 8, 4));
    assert(pe32_win32_set_file_pointer(first, (pe32_u32)-4, 0, 2)
           == sizeof(game_file_data) - 5);
    assert(pe32_win32_read_file(first, data, sizeof(data), &count, 0));
    assert(count == 4 && !memcmp(data, game_file_data + sizeof(game_file_data) - 5, 4));
    assert(pe32_win32_read_file(first, data, 1, &count, 0) && count == 0);
    assert(pe32_win32_set_file_pointer(first, (pe32_u32)-1, 0, 0)
           == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 131);
    assert(pe32_win32_set_file_pointer(first, 0, 0, 4) == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_write_file(first, "!", 1, &count, 0));
    assert(pe32_win32_get_last_error() == 5 && count == 0);
    assert(pe32_win32_close_handle(first));
    assert(!pe32_win32_close_handle(first));
    assert(pe32_win32_get_last_error() == 6);
    assert(!pe32_win32_read_file(first, data, 1, &count, 0));
    assert(pe32_win32_get_last_error() == 6);
    assert(!pe32_win32_get_file_time(first, &creation, 0, 0));
    assert(pe32_win32_get_last_error() == 6);
    assert(!pe32_win32_get_file_information_by_handle(first, &information));
    assert(pe32_win32_get_last_error() == 6);

    big = pe32_win32_create_file_a("Big.dat", 0x80000000u, 1, 0, 3, 0, 0);
    assert(big != PE32_INVALID_HANDLE_VALUE);
    high = 0;
    assert(pe32_win32_get_file_size(big, &high) == 0xffffffffu && high == 1);
    assert(pe32_win32_get_file_information_by_handle(big, &information));
    assert(information.size_low == 0xffffffffu && information.size_high == 1);
    assert(pe32_win32_get_last_error() == 0); /* 0xffffffff is ambiguous. */
    assert(pe32_win32_get_file_size(big, 0) == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 0); /* No high pointer: truncated. */
    high = 0;
    position = pe32_win32_set_file_pointer(big, 0xffffffffu, &high, 0);
    assert(position == 0xffffffffu && high == 0);
    assert(pe32_win32_get_last_error() == 0);
    assert(pe32_win32_set_file_pointer(big, 2, &high, 1) == 1 && high == 1);
    assert(pe32_win32_set_file_pointer(big, 0, 0, 2) == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 87); /* No high word, >4 GiB. */
    high = 0;
    assert(pe32_win32_set_file_pointer(big, 0, &high, 1) == 1 && high == 1);
    assert(pe32_win32_close_handle(big));

    metadata = pe32_win32_create_file_a("Media/Gear.wav", 0, 1, 0, 3, 0, 0);
    assert(metadata != PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_file_size(metadata, 0) == sizeof(game_file_data) - 1);
    assert(!pe32_win32_read_file(metadata, data, 1, &count, 0));
    assert(count == 0 && pe32_win32_get_last_error() == 5);
    assert(pe32_win32_set_file_pointer(metadata, 0, 0, 1) == 0xffffffffu);
    assert(pe32_win32_get_last_error() == 5);
    assert(pe32_win32_close_handle(metadata));

    /* Reopening and exhausting the guest handle table cannot leak fds. */
    for (i = 0; i < 64; ++i) {
        handles[i] = pe32_win32_create_file_a("Media/Gear.wav", 0, 1, 0, 3, 0, 0);
        /* The second handle remains open; 63 extra handles fit. */
        if (i < 63) {
            assert(handles[i] != PE32_INVALID_HANDLE_VALUE);
        } else {
            assert(handles[i] == PE32_INVALID_HANDLE_VALUE);
            assert(pe32_win32_get_last_error() == 4);
        }
    }
    assert(fake_game_opens == 67); /* two + large + metadata + 63 opens */
    pe32_win32_initialize(0, 0);
    assert(fake_game_closes == fake_game_opens);
    assert(pe32_win32_create_file_a("Media/Gear.wav", 0, 1, 0, 3, 0, 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 3); /* No image root. */
    pe32_io_set_file_backend(0, 0, 0, 0);
    pe32_io_set_stat_backend(0);
    pe32_io_set_backend(0, 0, 0);
}

static void test_win32_find_game_files(void)
{
    struct pe32_module modules[] = {exe, dll};
    struct pe32_find_data_a data;
    pe32_u32 first, handle, handles[32];
    int seen_readme = 0, seen_folder = 0;
    unsigned count, i;
    modules[0].path = "/opt/games/A-10Cuba/A10Cuba.exe";
    fake_dir_opens = fake_dir_closes = 0;
    memset(fake_directories, 0, sizeof(fake_directories));
    pe32_io_set_file_backend(fake_game_open, fake_game_close,
                             fake_game_seek, fake_game_size);
    pe32_io_set_directory_backend(fake_open_directory, fake_next_directory);
    pe32_win32_initialize(modules, 2);
    assert(sizeof(struct pe32_find_data_a) == 320);
    assert(pe32_win32_find_first_file_a(0, &data) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 123);
    assert(pe32_win32_find_first_file_a("/etc/*", &data) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_find_first_file_a("Media/../*", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_find_first_file_a("Media/*/*.wav", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_find_first_file_a("Media/", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_find_first_file_a("C:\\*", &data) == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_find_first_file_a("Media/*.wav", 0)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 87);
    assert(!fake_dir_opens);

    memset(&data, 0xff, sizeof(data));
    first = pe32_win32_find_first_file_a("Media\\*.WaV", &data);
    assert(first != PE32_INVALID_HANDLE_VALUE);
    assert(!strcmp(data.filename, "Gear.wav"));
    assert(data.attributes == 0x21);
    assert(data.size_low == strlen(data.filename) + 100);
    assert(data.size_high == 0);
    assert(data.alternate_filename[0] == 0);
    assert(data.reserved0 == 0 && data.reserved1 == 0);
    assert(pe32_win32_find_next_file_a(first, &data));
    assert(!strcmp(data.filename, "TAUNT2.WAV"));
    assert(data.attributes == 0x20);
    assert(!pe32_win32_find_next_file_a(first, &data));
    assert(pe32_win32_get_last_error() == 18); /* ERROR_NO_MORE_FILES */
    assert(!pe32_win32_close_handle(first)); /* FindClose, not CloseHandle. */
    assert(pe32_win32_get_last_error() == 6);
    assert(!pe32_win32_find_next_file_a(first, 0));
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_find_close(first));
    assert(!pe32_win32_find_close(first));
    assert(pe32_win32_get_last_error() == 6);

    handle = pe32_win32_find_first_file_a("Media\\*.*", &data);
    assert(handle != PE32_INVALID_HANDLE_VALUE);
    count = 0;
    do {
        ++count;
        if (!strcmp(data.filename, "README")) {
            seen_readme = 1; /* *.* matches extensionless names on Win32. */
        }
        if (!strcmp(data.filename, "SubFolder")) {
            seen_folder = 1;
            assert(data.attributes == 0x10);
        }
    } while (pe32_win32_find_next_file_a(handle, &data));
    assert(count == 5 && seen_readme && seen_folder);
    assert(pe32_win32_find_close(handle));

    handle = pe32_win32_find_first_file_a("Missions\\D-????Zone04.dmo", &data);
    assert(handle != PE32_INVALID_HANDLE_VALUE);
    assert(!strcmp(data.filename, "D-DropZone04.dmo"));
    assert(!pe32_win32_find_next_file_a(handle, &data));
    assert(pe32_win32_find_close(handle));
    handle = pe32_win32_find_first_file_a(".\\A10CUBA.exe", &data);
    assert(handle != PE32_INVALID_HANDLE_VALUE);
    assert(!strcmp(data.filename, "A10Cuba.exe"));
    assert(pe32_win32_find_close(handle));
    assert(pe32_win32_find_first_file_a("Media/absent*.dmo", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 2); /* ERROR_FILE_NOT_FOUND */
    assert(pe32_win32_find_first_file_a("Unlisted/*", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 2);

    for (i = 0; i < 32; ++i) {
        handles[i] = pe32_win32_find_first_file_a("Media/*.wav", &data);
        assert(handles[i] != PE32_INVALID_HANDLE_VALUE);
    }
    assert(pe32_win32_find_first_file_a("Media/*.wav", &data)
           == PE32_INVALID_HANDLE_VALUE);
    assert(pe32_win32_get_last_error() == 4); /* Guest table full. */
    pe32_win32_initialize(0, 0); /* Cleanup leaked find handles. */
    assert(fake_dir_opens == fake_dir_closes);
    assert(!pe32_win32_find_next_file_a(handles[0], &data));
    assert(pe32_win32_get_last_error() == 6);
    pe32_io_set_directory_backend(0, 0);
    pe32_io_set_file_backend(0, 0, 0, 0);
}

static void test_win32_private_heaps(void)
{
    void *heap, *second_heap;
    void *blocks[20];
    void *neighbor, *fence;
    unsigned char *a, *b, *guard, *moved, *zero;
    unsigned char *filled;
    unsigned i;
    mapped_regions = unmapped_regions = 0;
    pe32_heap_set_backend(host_heap_map, host_heap_unmap);
    pe32_win32_initialize(0, 0);

    pe32_win32_set_last_error(0xfefe);
    assert(!pe32_win32_heap_create(0x40000, 0, 0)); /* Executable unsupported. */
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_heap_create(0, 4096, 4096));
    assert(pe32_win32_get_last_error() == 87);
    assert(!pe32_win32_heap_create(4, 0, 0)); /* Cannot raise SEH. */
    assert(pe32_win32_get_last_error() == 87);

    heap = pe32_win32_heap_create(0, 0, 0);
    second_heap = pe32_win32_heap_create(1, 0, 8192);
    assert(heap && second_heap && heap != second_heap);
    assert(pe32_win32_heap_alloc(heap, 0, 0) != 0);

    a = pe32_win32_heap_alloc(heap, 8, 23);
    b = pe32_win32_heap_alloc(heap, 0, 40);
    guard = pe32_win32_heap_alloc(heap, 0, 16);
    assert(a && b && guard);
    assert(((uintptr_t)a & 7) == 0);
    assert(((uintptr_t)b & 7) == 0);
    assert(pe32_win32_heap_size(heap, 0, a) == 23);
    assert(pe32_win32_heap_size(heap, 0, b) == 40);
    assert(!pe32_win32_heap_alloc(heap, 0, 0xfffffffcu));
    for (i = 0; i < 23; ++i) {
        assert(a[i] == 0);
        a[i] = (unsigned char)(i + 1);
    }
    memset(b, 0x92, 40);

    /* HeapSize/Alloc/ReAlloc failures do not change last error. */
    pe32_win32_set_last_error(0x1234);
    assert(pe32_win32_heap_size(second_heap, 0, a) == 0xffffffffu);
    assert(!pe32_win32_heap_alloc(second_heap, 0, 0x7fff8));
    assert(!pe32_win32_heap_realloc(heap, 0x10, a, 100)); /* Guarded in place. */
    assert(pe32_win32_get_last_error() == 0x1234);
    assert(pe32_win32_heap_size(heap, 0, a) == 23);

    moved = pe32_win32_heap_realloc(heap, 8, a, 100);
    assert(moved && moved != a);
    assert(pe32_win32_heap_size(heap, 0, moved) == 100);
    for (i = 0; i < 100; ++i) {
        assert(moved[i] == (i < 23 ? (unsigned char)(i + 1) : 0));
    }
    assert(pe32_win32_heap_size(heap, 0, a) == 0xffffffffu);
    assert(pe32_win32_heap_free(heap, 0, a) == 0);
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_heap_free(second_heap, 0, moved) == 0);
    assert(pe32_win32_get_last_error() == 87);
    neighbor = pe32_win32_heap_alloc(heap, 0, 40);
    fence = pe32_win32_heap_alloc(heap, 0, 8);
    assert(neighbor && fence);
    assert(!pe32_win32_heap_realloc(heap, 0x10, moved, 112));

    assert(pe32_win32_heap_free(heap, 0, b));
    assert(pe32_win32_heap_free(heap, 0, neighbor));
    /* Freed adjacent block now permits the in-place growth. */
    filled = pe32_win32_heap_realloc(heap, 0x10 | 8, moved, 112);
    assert(filled == moved);
    assert(pe32_win32_heap_size(heap, 0, filled) == 112);
    for (i = 100; i < 112; ++i) {
        assert(filled[i] == 0);
    }
    assert(pe32_win32_heap_realloc(heap, 0, filled, 9) == filled);
    assert(pe32_win32_heap_size(heap, 0, filled) == 9);
    assert(!pe32_win32_heap_realloc(heap, 0x40000, filled, 12));
    assert(pe32_win32_heap_size(heap, 0, filled) == 9);

    zero = pe32_win32_heap_alloc(heap, 8, 32);
    assert(zero);
    for (i = 0; i < 32; ++i) {
        assert(zero[i] == 0);
    }
    assert(pe32_win32_heap_free(heap, 0, zero));
    assert(!pe32_win32_heap_free(heap, 8, filled));
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_heap_free(heap, 0, filled));
    assert(pe32_win32_heap_free(heap, 0, guard));
    assert(pe32_win32_heap_free(heap, 0, fence));
    assert(pe32_win32_heap_free(heap, 0, 0));

    /* Span multiple 64-KiB segments, free every block, and reuse capacity. */
    for (i = 0; i < 20; ++i) {
        blocks[i] = pe32_win32_heap_alloc(heap, 0, 8192);
        assert(blocks[i]);
    }
    for (i = 0; i < 20; ++i) {
        assert(pe32_win32_heap_free(heap, 0, blocks[i]));
    }
    assert(pe32_win32_heap_alloc(heap, 0, 16000));

    assert(pe32_win32_heap_alloc(second_heap, 0, 8170) == 0);
    assert(pe32_win32_heap_alloc(second_heap, 0, 1024));
    assert(pe32_win32_heap_destroy(second_heap));
    assert(!pe32_win32_heap_destroy(second_heap));
    assert(pe32_win32_get_last_error() == 6);
    assert(pe32_win32_heap_destroy(heap));
    assert(mapped_regions == unmapped_regions);
    heap = pe32_win32_heap_create(0, 0, 0);
    assert(pe32_win32_heap_alloc(heap, 0, 64));
    pe32_win32_initialize(0, 0); /* Release leaked private heaps. */
    assert(mapped_regions == unmapped_regions);
    pe32_heap_set_backend(0, 0);
}

int main(void)
{
    test_named_and_ordinal();
    test_fallback_lookup_and_name();
    test_no_partial_bind_on_missing();
    test_reject_bad_images_and_forwarders();
    test_explicit_host_resolver();
    test_preloaded_win32_module_lookups();
    test_win32_tls_and_last_error();
    test_win32_process_startup_subset();
    test_win32_strings_and_interlocked();
    test_win32_single_thread_critical_sections();
    test_win32_standard_io();
    test_win32_readonly_game_files();
    test_win32_find_game_files();
    test_win32_private_heaps();
    puts("PE32 linker: imports, DLL lookup, startup, strings, synchronization, I/O, TLS, heaps and bounds tests passed");
    return 0;
}

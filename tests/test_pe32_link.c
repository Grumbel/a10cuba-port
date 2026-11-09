/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Host-native tests of the exact resolver built into the Linux/i386 loader. */
#include "pe32_link.h"
#include "pe32_win32.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

int main(void)
{
    test_named_and_ordinal();
    test_fallback_lookup_and_name();
    test_no_partial_bind_on_missing();
    test_reject_bad_images_and_forwarders();
    test_explicit_host_resolver();
    test_preloaded_win32_module_lookups();
    test_win32_tls_and_last_error();
    puts("PE32 module linker: imports, DLL lookup, TLS and bounds tests passed");
    return 0;
}

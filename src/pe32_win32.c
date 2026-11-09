/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_win32.h"

static const struct pe32_module *registered_modules;
static pe32_u32 registered_count;

#if defined(__i386__)
static int equal_ascii_ci(const char *a, const char *b)
{
    unsigned char left, right;
    if (!a || !b) {
        return 0;
    }
    for (; *a && *b; ++a, ++b) {
        left = (unsigned char)*a;
        right = (unsigned char)*b;
        if (left >= 'A' && left <= 'Z') {
            left += 'a' - 'A';
        }
        if (right >= 'A' && right <= 'Z') {
            right += 'a' - 'A';
        }
        if (left != right) {
            return 0;
        }
    }
    return *a == *b;
}

static int equal_ascii(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    for (; *a && *b && *a == *b; ++a, ++b) {
        /* Exact matching for exported function names. */
    }
    return *a == *b;
}
#endif

void pe32_win32_initialize(const struct pe32_module *modules, pe32_u32 count)
{
    registered_modules = modules;
    registered_count = modules ? count : 0;
}

pe32_u32 PE32_WINAPI pe32_win32_load_library_a(const char *name)
{
    const struct pe32_module *module =
        pe32_find_module(registered_modules, registered_count, name);
    return module ? module->base : 0;
}

pe32_u32 PE32_WINAPI pe32_win32_get_module_handle_a(const char *name)
{
    if (!name) {
        return registered_count ? registered_modules[0].base : 0;
    }
    return pe32_win32_load_library_a(name);
}

pe32_u32 PE32_WINAPI pe32_win32_get_proc_address(pe32_u32 handle,
                                                  const char *name_or_ordinal)
{
    const struct pe32_module *module = pe32_find_module_base(
        registered_modules, registered_count, handle);
    enum pe32_link_status result;
    pe32_u32 ordinal = 0, address = 0;
    const char *name = name_or_ordinal;
    if (!module || !name_or_ordinal) {
        return 0;
    }
    if ((unsigned long)name_or_ordinal <= 0xfffful) {
        ordinal = (pe32_u32)(unsigned long)name_or_ordinal;
        name = (const char *)0;
    }
    result = pe32_find_export(module, name, ordinal, &address);
    return result == PE32_LINK_OK ? address : 0;
}

enum pe32_link_status pe32_win32_resolve_import(
    void *context, const char *dll, const char *name, pe32_u32 ordinal,
    pe32_u32 *address)
{
    (void)context;
    (void)ordinal;
    if (address) {
        *address = 0;
    }
#if defined(__i386__)
    if (!equal_ascii_ci(dll, "KERNEL32.dll") || !name || !address) {
        return PE32_LINK_MISSING;
    }
    if (equal_ascii(name, "LoadLibraryA")) {
        *address = (pe32_u32)(unsigned long)&pe32_win32_load_library_a;
    } else if (equal_ascii(name, "GetProcAddress")) {
        *address = (pe32_u32)(unsigned long)&pe32_win32_get_proc_address;
    } else if (equal_ascii(name, "GetModuleHandleA")) {
        *address = (pe32_u32)(unsigned long)&pe32_win32_get_module_handle_a;
    } else {
        return PE32_LINK_MISSING;
    }
    return PE32_LINK_OK;
#else
    (void)dll;
    (void)name;
    return PE32_LINK_MISSING;
#endif
}

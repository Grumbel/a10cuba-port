/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_win32.h"
#include "pe32_heap.h"

static const struct pe32_module *registered_modules;
static pe32_u32 registered_count;

enum {
    PE32_TLS_SLOT_COUNT = 64,
    PE32_ERROR_SUCCESS = 0,
    PE32_ERROR_NOT_ENOUGH_MEMORY = 8,
    PE32_ERROR_INVALID_PARAMETER = 87
};

static pe32_u32 current_last_error;
static pe32_u8 tls_allocated[PE32_TLS_SLOT_COUNT];
static pe32_u32 tls_values[PE32_TLS_SLOT_COUNT];

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
    pe32_u32 i;
    pe32_heap_reset();
    registered_modules = modules;
    registered_count = modules ? count : 0;
    current_last_error = 0;
    for (i = 0; i < PE32_TLS_SLOT_COUNT; ++i) {
        tls_allocated[i] = 0;
        tls_values[i] = 0;
    }
}

void PE32_WINAPI pe32_win32_set_last_error(pe32_u32 error)
{
    current_last_error = error;
}

pe32_u32 PE32_WINAPI pe32_win32_get_last_error(void)
{
    return current_last_error;
}

pe32_u32 PE32_WINAPI pe32_win32_tls_alloc(void)
{
    pe32_u32 i;
    for (i = 0; i < PE32_TLS_SLOT_COUNT; ++i) {
        if (!tls_allocated[i]) {
            tls_allocated[i] = 1;
            tls_values[i] = 0;
            return i;
        }
    }
    current_last_error = PE32_ERROR_NOT_ENOUGH_MEMORY;
    return PE32_TLS_OUT_OF_INDEXES;
}

pe32_u32 PE32_WINAPI pe32_win32_tls_free(pe32_u32 index)
{
    if (index >= PE32_TLS_SLOT_COUNT || !tls_allocated[index]) {
        current_last_error = PE32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    tls_allocated[index] = 0;
    tls_values[index] = 0;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_tls_get_value(pe32_u32 index)
{
    if (index >= PE32_TLS_SLOT_COUNT || !tls_allocated[index]) {
        current_last_error = PE32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    /* Unlike most Win32 APIs, TlsGetValue clears last error on success,
     * including a successful lookup of a null pointer. */
    current_last_error = PE32_ERROR_SUCCESS;
    return tls_values[index];
}

pe32_u32 PE32_WINAPI pe32_win32_tls_set_value(pe32_u32 index, pe32_u32 value)
{
    /* Win32's TlsSetValue performs minimal validation for the first 64
     * indices: a released index in this range can still accept a write.
     * TlsAlloc clears the slot on subsequent reallocation. */
    if (index >= PE32_TLS_SLOT_COUNT) {
        current_last_error = PE32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    tls_values[index] = value;
    return 1;
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
#define EXPOSE_KERNEL32(legacy_name, implementation) \
    if (equal_ascii(name, legacy_name)) { \
        *address = (pe32_u32)(unsigned long)&implementation; \
        return PE32_LINK_OK; \
    }
    EXPOSE_KERNEL32("LoadLibraryA", pe32_win32_load_library_a)
    EXPOSE_KERNEL32("GetProcAddress", pe32_win32_get_proc_address)
    EXPOSE_KERNEL32("GetModuleHandleA", pe32_win32_get_module_handle_a)
    EXPOSE_KERNEL32("SetLastError", pe32_win32_set_last_error)
    EXPOSE_KERNEL32("GetLastError", pe32_win32_get_last_error)
    EXPOSE_KERNEL32("TlsAlloc", pe32_win32_tls_alloc)
    EXPOSE_KERNEL32("TlsFree", pe32_win32_tls_free)
    EXPOSE_KERNEL32("TlsGetValue", pe32_win32_tls_get_value)
    EXPOSE_KERNEL32("TlsSetValue", pe32_win32_tls_set_value)
    EXPOSE_KERNEL32("HeapCreate", pe32_win32_heap_create)
    EXPOSE_KERNEL32("HeapDestroy", pe32_win32_heap_destroy)
    EXPOSE_KERNEL32("HeapAlloc", pe32_win32_heap_alloc)
    EXPOSE_KERNEL32("HeapReAlloc", pe32_win32_heap_realloc)
    EXPOSE_KERNEL32("HeapFree", pe32_win32_heap_free)
    EXPOSE_KERNEL32("HeapSize", pe32_win32_heap_size)
#undef EXPOSE_KERNEL32
    return PE32_LINK_MISSING;
#else
    (void)dll;
    (void)name;
    return PE32_LINK_MISSING;
#endif
}

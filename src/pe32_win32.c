/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_win32.h"
#include "pe32_heap.h"

static const struct pe32_module *registered_modules;
static pe32_u32 registered_count;

enum {
    PE32_TLS_SLOT_COUNT = 64,
    PE32_COMMAND_LINE_CAPACITY = 4096,
    PE32_ERROR_SUCCESS = 0,
    PE32_ERROR_NOT_ENOUGH_MEMORY = 8,
    PE32_ERROR_INVALID_PARAMETER = 87,
    PE32_ERROR_INSUFFICIENT_BUFFER = 122,
    PE32_ERROR_MOD_NOT_FOUND = 126
};

static pe32_u32 current_last_error;
static pe32_u8 tls_allocated[PE32_TLS_SLOT_COUNT];
static pe32_u32 tls_values[PE32_TLS_SLOT_COUNT];
static char command_line[PE32_COMMAND_LINE_CAPACITY];
static char empty_environment_a[2];
static pe32_u16 empty_environment_w[2];

static pe32_u32 string_length(const char *str)
{
    pe32_u32 length = 0;
    while (str[length]) {
        ++length;
    }
    return length;
}

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
    const char *path;
    pe32_u32 length = 0;
    pe32_heap_reset();
    registered_modules = modules;
    registered_count = modules ? count : 0;
    current_last_error = 0;
    for (i = 0; i < PE32_TLS_SLOT_COUNT; ++i) {
        tls_allocated[i] = 0;
        tls_values[i] = 0;
    }
    command_line[0] = 0;
    path = registered_count ? registered_modules[0].path : (const char *)0;
    if (path) {
        /* A single quoted program argument, never the loader's own options.
         * Windows forbids double quotes in filenames; Linux paths with a
         * quote cannot be represented faithfully by this early shim. */
        command_line[length++] = '"';
        for (i = 0; path[i] && length < PE32_COMMAND_LINE_CAPACITY - 2; ++i) {
            command_line[length++] = path[i];
        }
        command_line[length++] = '"';
        command_line[length] = 0;
    }
}

char *PE32_WINAPI pe32_win32_get_command_line_a(void)
{
    return command_line;
}

pe32_u32 PE32_WINAPI pe32_win32_get_module_file_name_a(pe32_u32 handle,
                                                        char *buffer,
                                                        pe32_u32 capacity)
{
    const struct pe32_module *module;
    const char *path;
    pe32_u32 length, i, copied;
    if (!registered_count) {
        current_last_error = PE32_ERROR_MOD_NOT_FOUND;
        return 0;
    }
    module = handle ? pe32_find_module_base(registered_modules,
                                            registered_count, handle)
                    : &registered_modules[0];
    if (!module || !module->path) {
        current_last_error = PE32_ERROR_MOD_NOT_FOUND;
        return 0;
    }
    path = module->path;
    if (!buffer || !capacity) {
        current_last_error = PE32_ERROR_INSUFFICIENT_BUFFER;
        return 0;
    }
    length = string_length(path);
    copied = length < capacity ? length : capacity - 1;
    for (i = 0; i < copied; ++i) {
        buffer[i] = path[i];
    }
    buffer[copied] = 0;
    if (length >= capacity) {
        current_last_error = PE32_ERROR_INSUFFICIENT_BUFFER;
        return capacity;
    }
    return copied;
}

void PE32_WINAPI pe32_win32_get_startup_info_a(void *startup_info)
{
    pe32_u8 *bytes = (pe32_u8 *)startup_info;
    pe32_u32 i;
    if (!bytes) {
        return;
    }
    for (i = 0; i < PE32_STARTUPINFOA_SIZE; ++i) {
        bytes[i] = 0;
    }
    /* DWORD cb, followed by zeroed pointer/window/CRT/std-handle fields.
     * No STARTF_USESTDHANDLES: Win32 file handles do not exist yet. */
    bytes[0] = PE32_STARTUPINFOA_SIZE;
}

char *PE32_WINAPI pe32_win32_get_environment_strings(void)
{
    return empty_environment_a;
}

pe32_u16 *PE32_WINAPI pe32_win32_get_environment_strings_w(void)
{
    return empty_environment_w;
}

pe32_u32 PE32_WINAPI pe32_win32_free_environment_strings_a(char *environment)
{
    if (environment == empty_environment_a) {
        return 1;
    }
    current_last_error = PE32_ERROR_INVALID_PARAMETER;
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_free_environment_strings_w(pe32_u16 *environment)
{
    if (environment == empty_environment_w) {
        return 1;
    }
    current_last_error = PE32_ERROR_INVALID_PARAMETER;
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_get_current_process(void)
{
    return 0xffffffffu; /* Win32 GetCurrentProcess pseudo-handle, (HANDLE)-1 */
}

pe32_u32 PE32_WINAPI pe32_win32_get_current_thread(void)
{
    return 0xfffffffeu; /* Win32 GetCurrentThread pseudo-handle, (HANDLE)-2 */
}

pe32_u32 PE32_WINAPI pe32_win32_get_current_thread_id(void)
{
    return 1; /* Only one synthetic Win32 thread exists in this bootstrap. */
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
    EXPOSE_KERNEL32("GetCommandLineA", pe32_win32_get_command_line_a)
    EXPOSE_KERNEL32("GetModuleFileNameA", pe32_win32_get_module_file_name_a)
    EXPOSE_KERNEL32("GetStartupInfoA", pe32_win32_get_startup_info_a)
    EXPOSE_KERNEL32("GetEnvironmentStrings", pe32_win32_get_environment_strings)
    EXPOSE_KERNEL32("GetEnvironmentStringsW", pe32_win32_get_environment_strings_w)
    EXPOSE_KERNEL32("FreeEnvironmentStringsA", pe32_win32_free_environment_strings_a)
    EXPOSE_KERNEL32("FreeEnvironmentStringsW", pe32_win32_free_environment_strings_w)
    EXPOSE_KERNEL32("GetCurrentProcess", pe32_win32_get_current_process)
    EXPOSE_KERNEL32("GetCurrentThread", pe32_win32_get_current_thread)
    EXPOSE_KERNEL32("GetCurrentThreadId", pe32_win32_get_current_thread_id)
#undef EXPOSE_KERNEL32
    return PE32_LINK_MISSING;
#else
    (void)dll;
    (void)name;
    return PE32_LINK_MISSING;
#endif
}

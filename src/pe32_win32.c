/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_win32.h"
#include "pe32_proc.h"
#include "pe32_text.h"
#include "pe32_res.h"
#include "pe32_misc.h"
#include "pe32_winmm.h"
#include "pe32_sys.h"
#include "pe32_stubs.h"
#include "pe32_heap.h"
#include "pe32_io.h"

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

typedef char pe32_critical_section_size_check[
    sizeof(struct pe32_critical_section) == 24 ? 1 : -1];

static pe32_u32 string_length(const char *str)
{
    pe32_u32 length = 0;
    while (str[length]) {
        ++length;
    }
    return length;
}

/* The loader opens real Linux files, but the guest sees a small virtual C:\
 * rooted at the executable's directory. Keep Win32 paths supplied by host
 * tests unchanged; translate Linux module paths to C:\<basename> so the
 * process command line agrees with GetFullPathNameA and CreateFileA. */
static const char *guest_module_path(const char *path, char *buffer,
                                     pe32_u32 capacity)
{
    const char *basename = path;
    pe32_u32 i, length;
    if (!path) {
        return 0;
    }
    if (path[0] && path[1] == ':') {
        return path;
    }
    for (i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            basename = path + i + 1;
        }
    }
    length = string_length(basename);
    if (length + 4 > capacity) {
        return 0;
    }
    buffer[0] = 'C';
    buffer[1] = ':';
    buffer[2] = '\\';
    for (i = 0; i <= length; ++i) {
        buffer[i + 3] = basename[i];
    }
    return buffer;
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
    char guest_path[PE32_COMMAND_LINE_CAPACITY];
    pe32_u32 length = 0;
    pe32_heap_reset();
    pe32_io_reset();
    registered_modules = modules;
    registered_count = modules ? count : 0;
    pe32_io_set_image_path(registered_count ? registered_modules[0].path : 0);
    current_last_error = 0;
    for (i = 0; i < PE32_TLS_SLOT_COUNT; ++i) {
        tls_allocated[i] = 0;
        tls_values[i] = 0;
    }
    command_line[0] = 0;
    path = registered_count ?
        guest_module_path(registered_modules[0].path, guest_path,
                          sizeof(guest_path)) : (const char *)0;
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
    char guest_path[PE32_COMMAND_LINE_CAPACITY];
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
    path = guest_module_path(module->path, guest_path, sizeof(guest_path));
    if (!path) {
        current_last_error = PE32_ERROR_INSUFFICIENT_BUFFER;
        return 0;
    }
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

int PE32_WINAPI pe32_win32_lstrlen_a(const char *string)
{
    return string ? (int)string_length(string) : 0;
}

char *PE32_WINAPI pe32_win32_lstrcpy_a(char *dest, const char *source)
{
    char *output = dest;
    while ((*dest++ = *source++) != 0) {
        /* Copy through the terminating byte. */
    }
    return output;
}

char *PE32_WINAPI pe32_win32_lstrcpyn_a(char *dest, const char *source,
                                        int max_length)
{
    int i;
    if (max_length <= 0) {
        return (char *)0;
    }
    for (i = 0; i < max_length - 1 && source[i]; ++i) {
        dest[i] = source[i];
    }
    dest[i] = 0;
    return dest;
}

char *PE32_WINAPI pe32_win32_lstrcat_a(char *dest, const char *source)
{
    char *output = dest;
    dest += string_length(dest);
    while ((*dest++ = *source++) != 0) {
        /* Append through the terminating byte. */
    }
    return output;
}

pe32_u32 PE32_WINAPI pe32_win32_interlocked_increment(volatile pe32_u32 *value)
{
    return __atomic_add_fetch(value, 1u, __ATOMIC_SEQ_CST);
}

pe32_u32 PE32_WINAPI pe32_win32_interlocked_decrement(volatile pe32_u32 *value)
{
    return __atomic_sub_fetch(value, 1u, __ATOMIC_SEQ_CST);
}

void PE32_WINAPI pe32_win32_initialize_critical_section(
    struct pe32_critical_section *section)
{
    /* 1990s Windows critical sections started with LockCount == -1. The
     * DebugInfo allocation used by Windows is intentionally absent: nobody
     * may dereference this as a real NT RTL_CRITICAL_SECTION internals. */
    section->debug_info = 0;
    section->lock_count = 0xffffffffu;
    section->recursion_count = 0;
    section->owning_thread = 0;
    section->lock_semaphore = 0;
    section->spin_count = 0;
}

void PE32_WINAPI pe32_win32_enter_critical_section(
    struct pe32_critical_section *section)
{
    /* Only thread ID 1 can exist in this bootstrap. Once CreateThread is
     * implemented, this MUST be replaced by real contention handling. */
    if (!section->recursion_count) {
        section->owning_thread = 1;
    }
    ++section->recursion_count;
    ++section->lock_count;
}

void PE32_WINAPI pe32_win32_leave_critical_section(
    struct pe32_critical_section *section)
{
    /* Leaving without owning the lock is invalid in Win32. Ignore it here
     * rather than corrupting the single-thread bookkeeping. */
    if (!section->recursion_count || section->owning_thread != 1) {
        return;
    }
    --section->recursion_count;
    --section->lock_count;
    if (!section->recursion_count) {
        section->owning_thread = 0;
    }
}

void PE32_WINAPI pe32_win32_delete_critical_section(
    struct pe32_critical_section *section)
{
    /* No kernel semaphore or heap-allocated DebugInfo was created. */
    section->debug_info = 0;
    section->lock_count = 0xffffffffu;
    section->recursion_count = 0;
    section->owning_thread = 0;
    section->lock_semaphore = 0;
    section->spin_count = 0;
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

const struct pe32_module *pe32_win32_module_for_handle(pe32_u32 handle)
{
    if (!registered_count) {
        return (const struct pe32_module *)0;
    }
    return handle ? pe32_find_module_base(registered_modules, registered_count,
                                          handle)
                  : &registered_modules[0];
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
    if (!address) {
        return PE32_LINK_MISSING;
    }
    if (!name) {
        return pe32_stubs_resolve(dll, name, ordinal, address);
    }
    if (equal_ascii_ci(dll, "WINMM.dll")) {
        return pe32_winmm_resolve(name, address);
    }
    if (!equal_ascii_ci(dll, "KERNEL32.dll")) {
        if (pe32_sys_resolve(dll, name, address) == PE32_LINK_OK) {
            return PE32_LINK_OK;
        }
        return pe32_stubs_resolve(dll, name, ordinal, address);
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
    EXPOSE_KERNEL32("lstrlenA", pe32_win32_lstrlen_a)
    EXPOSE_KERNEL32("lstrcpyA", pe32_win32_lstrcpy_a)
    EXPOSE_KERNEL32("lstrcpynA", pe32_win32_lstrcpyn_a)
    EXPOSE_KERNEL32("lstrcatA", pe32_win32_lstrcat_a)
    EXPOSE_KERNEL32("InterlockedIncrement", pe32_win32_interlocked_increment)
    EXPOSE_KERNEL32("InterlockedDecrement", pe32_win32_interlocked_decrement)
    EXPOSE_KERNEL32("InitializeCriticalSection", pe32_win32_initialize_critical_section)
    EXPOSE_KERNEL32("EnterCriticalSection", pe32_win32_enter_critical_section)
    EXPOSE_KERNEL32("LeaveCriticalSection", pe32_win32_leave_critical_section)
    EXPOSE_KERNEL32("DeleteCriticalSection", pe32_win32_delete_critical_section)
    EXPOSE_KERNEL32("GetStdHandle", pe32_win32_get_std_handle)
    EXPOSE_KERNEL32("SetStdHandle", pe32_win32_set_std_handle)
    EXPOSE_KERNEL32("SetHandleCount", pe32_win32_set_handle_count)
    EXPOSE_KERNEL32("GetFileType", pe32_win32_get_file_type)
    EXPOSE_KERNEL32("ReadFile", pe32_win32_read_file)
    EXPOSE_KERNEL32("WriteFile", pe32_win32_write_file)
    EXPOSE_KERNEL32("CloseHandle", pe32_win32_close_handle)
    EXPOSE_KERNEL32("GetFullPathNameA", pe32_win32_get_full_path_name_a)
    EXPOSE_KERNEL32("CreateFileA", pe32_win32_create_file_a)
    EXPOSE_KERNEL32("GetFileSize", pe32_win32_get_file_size)
    EXPOSE_KERNEL32("SetFilePointer", pe32_win32_set_file_pointer)
    EXPOSE_KERNEL32("GetFileTime", pe32_win32_get_file_time)
    EXPOSE_KERNEL32("GetFileInformationByHandle",
                    pe32_win32_get_file_information_by_handle)
    EXPOSE_KERNEL32("FindFirstFileA", pe32_win32_find_first_file_a)
    EXPOSE_KERNEL32("FindNextFileA", pe32_win32_find_next_file_a)
    EXPOSE_KERNEL32("FindClose", pe32_win32_find_close)
    EXPOSE_KERNEL32("GlobalAlloc", pe32_win32_global_alloc)
    EXPOSE_KERNEL32("GlobalReAlloc", pe32_win32_global_realloc)
    EXPOSE_KERNEL32("GlobalFree", pe32_win32_global_free)
    EXPOSE_KERNEL32("GlobalLock", pe32_win32_global_lock)
    EXPOSE_KERNEL32("GlobalUnlock", pe32_win32_global_unlock)
    EXPOSE_KERNEL32("GlobalHandle", pe32_win32_global_handle)
    EXPOSE_KERNEL32("GlobalFlags", pe32_win32_global_flags)
    EXPOSE_KERNEL32("GlobalMemoryStatus", pe32_win32_global_memory_status)
    EXPOSE_KERNEL32("LocalAlloc", pe32_win32_global_alloc)
    EXPOSE_KERNEL32("LocalReAlloc", pe32_win32_global_realloc)
    EXPOSE_KERNEL32("LocalFree", pe32_win32_global_free)
    EXPOSE_KERNEL32("ExitProcess", pe32_win32_exit_process)
    EXPOSE_KERNEL32("TerminateProcess", pe32_win32_terminate_process)
    EXPOSE_KERNEL32("ExitThread", pe32_win32_exit_thread)
    EXPOSE_KERNEL32("Sleep", pe32_win32_sleep)
    EXPOSE_KERNEL32("OutputDebugStringA", pe32_win32_output_debug_string_a)
    EXPOSE_KERNEL32("SetErrorMode", pe32_win32_set_error_mode)
    EXPOSE_KERNEL32("SetUnhandledExceptionFilter", pe32_win32_set_unhandled_exception_filter)
    EXPOSE_KERNEL32("UnhandledExceptionFilter", pe32_win32_unhandled_exception_filter)
    EXPOSE_KERNEL32("RaiseException", pe32_win32_raise_exception)
    EXPOSE_KERNEL32("RtlUnwind", pe32_win32_rtl_unwind)
    EXPOSE_KERNEL32("VirtualProtect", pe32_win32_virtual_protect)
    EXPOSE_KERNEL32("FreeLibrary", pe32_win32_free_library)
    EXPOSE_KERNEL32("FlushFileBuffers", pe32_win32_flush_file_buffers)
    EXPOSE_KERNEL32("CreateEventA", pe32_win32_create_event_a)
    EXPOSE_KERNEL32("SetEvent", pe32_win32_set_event)
    EXPOSE_KERNEL32("WaitForSingleObject", pe32_win32_wait_for_single_object)
    EXPOSE_KERNEL32("CreateThread", pe32_win32_create_thread)
    EXPOSE_KERNEL32("SuspendThread", pe32_win32_suspend_thread)
    EXPOSE_KERNEL32("ResumeThread", pe32_win32_resume_thread)
    EXPOSE_KERNEL32("SetThreadPriority", pe32_win32_set_thread_priority)
    EXPOSE_KERNEL32("DuplicateHandle", pe32_win32_duplicate_handle)
    EXPOSE_KERNEL32("lstrcmpA", pe32_win32_lstrcmp_a)
    EXPOSE_KERNEL32("lstrcmpiA", pe32_win32_lstrcmpi_a)
    EXPOSE_KERNEL32("MulDiv", pe32_win32_mul_div)
    EXPOSE_KERNEL32("IsBadReadPtr", pe32_win32_is_bad_read_ptr)
    EXPOSE_KERNEL32("IsBadWritePtr", pe32_win32_is_bad_write_ptr)
    EXPOSE_KERNEL32("IsBadCodePtr", pe32_win32_is_bad_code_ptr)
    EXPOSE_KERNEL32("GetACP", pe32_win32_get_acp)
    EXPOSE_KERNEL32("GetOEMCP", pe32_win32_get_oemcp)
    EXPOSE_KERNEL32("GetCPInfo", pe32_win32_get_cp_info)
    EXPOSE_KERNEL32("GetVersion", pe32_win32_get_version)
    EXPOSE_KERNEL32("MultiByteToWideChar", pe32_win32_multi_byte_to_wide_char)
    EXPOSE_KERNEL32("WideCharToMultiByte", pe32_win32_wide_char_to_multi_byte)
    EXPOSE_KERNEL32("GetStringTypeA", pe32_win32_get_string_type_a)
    EXPOSE_KERNEL32("GetStringTypeW", pe32_win32_get_string_type_w)
    EXPOSE_KERNEL32("LCMapStringA", pe32_win32_lc_map_string_a)
    EXPOSE_KERNEL32("LCMapStringW", pe32_win32_lc_map_string_w)
    EXPOSE_KERNEL32("CompareStringA", pe32_win32_compare_string_a)
    EXPOSE_KERNEL32("CompareStringW", pe32_win32_compare_string_w)
    EXPOSE_KERNEL32("GetLocaleInfoA", pe32_win32_get_locale_info_a)
    EXPOSE_KERNEL32("GetLocaleInfoW", pe32_win32_get_locale_info_w)
    EXPOSE_KERNEL32("FindResourceA", pe32_win32_find_resource_a)
    EXPOSE_KERNEL32("LoadResource", pe32_win32_load_resource)
    EXPOSE_KERNEL32("LockResource", pe32_win32_lock_resource)
    EXPOSE_KERNEL32("SizeofResource", pe32_win32_sizeof_resource)
    EXPOSE_KERNEL32("FreeResource", pe32_win32_free_resource)
    EXPOSE_KERNEL32("FileTimeToLocalFileTime", pe32_win32_file_time_to_local_file_time)
    EXPOSE_KERNEL32("FileTimeToSystemTime", pe32_win32_file_time_to_system_time)
    EXPOSE_KERNEL32("GetTimeZoneInformation", pe32_win32_get_time_zone_information)
    EXPOSE_KERNEL32("OpenFile", pe32_win32_open_file)
    EXPOSE_KERNEL32("_lclose", pe32_win32__lclose)
    EXPOSE_KERNEL32("_llseek", pe32_win32__llseek)
    EXPOSE_KERNEL32("GlobalAddAtomA", pe32_win32_global_add_atom_a)
    EXPOSE_KERNEL32("GlobalDeleteAtom", pe32_win32_global_delete_atom)
    EXPOSE_KERNEL32("GlobalGetAtomNameA", pe32_win32_global_get_atom_name_a)
    EXPOSE_KERNEL32("WritePrivateProfileStringA", pe32_win32_write_private_profile_string_a)
    EXPOSE_KERNEL32("SetEnvironmentVariableA", pe32_win32_set_environment_variable_a)
    EXPOSE_KERNEL32("GetProcessVersion", pe32_win32_get_process_version)
    EXPOSE_KERNEL32("WinExec", pe32_win32_win_exec)
    EXPOSE_KERNEL32("GetVolumeInformationA", pe32_win32_get_volume_information_a)
    EXPOSE_KERNEL32("LockFile", pe32_win32_lock_file)
    EXPOSE_KERNEL32("UnlockFile", pe32_win32_lock_file)
    EXPOSE_KERNEL32("SetEndOfFile", pe32_win32_set_end_of_file)
    EXPOSE_KERNEL32("PeekNamedPipe", pe32_win32_peek_named_pipe)
#undef EXPOSE_KERNEL32
    return PE32_LINK_MISSING;
#else
    (void)dll;
    (void)name;
    return PE32_LINK_MISSING;
#endif
}

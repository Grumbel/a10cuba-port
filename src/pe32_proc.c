/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_proc.h"
#include "pe32_heap.h"

enum {
    ERROR_INVALID_HANDLE = 6,
    ERROR_NOT_ENOUGH_MEMORY = 8,
    ERROR_CALL_NOT_IMPLEMENTED = 120,
    EVENT_BASE = 0x4000,
    EVENT_STEP = 4,
    EVENT_COUNT = 32,
    HEAP_ZERO_MEMORY = 8
};

static pe32_proc_exit_fn backend_exit;
static pe32_proc_sleep_fn backend_sleep;
static void *process_heap;
static pe32_u32 last_filter;
static struct {
    pe32_u8 active, manual, signalled;
} events[EVENT_COUNT];

void pe32_proc_set_backend(pe32_proc_exit_fn exit_fn,
                           pe32_proc_sleep_fn sleep_fn)
{
    backend_exit = exit_fn;
    backend_sleep = sleep_fn;
}

void pe32_proc_reset(void)
{
    unsigned i;
    process_heap = 0;
    last_filter = 0;
    for (i = 0; i < EVENT_COUNT; i++) {
        events[i].active = events[i].manual = events[i].signalled = 0;
    }
}

static void *global_heap(void)
{
    if (!process_heap) {
        process_heap = pe32_win32_heap_create(0, 0, 0);
    }
    return process_heap;
}

void *PE32_WINAPI pe32_win32_global_alloc(pe32_u32 flags, pe32_u32 bytes)
{
    void *heap = global_heap();
    void *memory = heap ? pe32_win32_heap_alloc(
        heap, (flags & PE32_GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0,
        bytes ? bytes : 1) : 0;
    if (!memory) {
        pe32_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    }
    return memory;
}

void *PE32_WINAPI pe32_win32_global_realloc(void *memory, pe32_u32 bytes,
                                            pe32_u32 flags)
{
    void *heap = global_heap();
    void *result;
    if (!heap) {
        return 0;
    }
    if (!memory) {
        return pe32_win32_global_alloc(flags, bytes);
    }
    result = pe32_win32_heap_realloc(
        heap, (flags & PE32_GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0, memory,
        bytes ? bytes : 1);
    if (!result) {
        pe32_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    }
    return result;
}

/* Returns NULL on success, the handle on failure, like Windows. */
void *PE32_WINAPI pe32_win32_global_free(void *memory)
{
    if (!memory) {
        return 0;
    }
    if (!pe32_win32_heap_free(global_heap(), 0, memory)) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return memory;
    }
    return 0;
}

void *PE32_WINAPI pe32_win32_global_lock(void *memory) { return memory; }
pe32_u32 PE32_WINAPI pe32_win32_global_unlock(void *memory)
{
    (void)memory;
    return 0; /* Lock count is always zero for fixed blocks. */
}
void *PE32_WINAPI pe32_win32_global_handle(void *memory) { return memory; }
pe32_u32 PE32_WINAPI pe32_win32_global_flags(void *memory)
{
    (void)memory;
    return 0;
}

void PE32_WINAPI pe32_win32_global_memory_status(void *status)
{
    pe32_u32 *words = status;
    unsigned i;
    /* MEMORYSTATUS: length, load, total/avail phys, page file, virtual. */
    words[0] = 32;
    words[1] = 10;
    for (i = 2; i < 8; i++) {
        words[i] = 0x10000000u;
    }
}

void PE32_WINAPI pe32_win32_exit_process(pe32_u32 code)
{
    if (backend_exit) {
        backend_exit(code);
    }
    for (;;) {
        /* A missing backend cannot return into guest code. */
    }
}

pe32_u32 PE32_WINAPI pe32_win32_terminate_process(pe32_u32 process,
                                                  pe32_u32 code)
{
    (void)process;
    pe32_win32_exit_process(code);
    return 0;
}

void PE32_WINAPI pe32_win32_exit_thread(pe32_u32 code)
{
    pe32_win32_exit_process(code); /* The only thread is the process. */
}

void PE32_WINAPI pe32_win32_sleep(pe32_u32 milliseconds)
{
    if (backend_sleep) {
        backend_sleep(milliseconds);
    }
}

void PE32_WINAPI pe32_win32_output_debug_string_a(const char *text)
{
    (void)text;
}

pe32_u32 PE32_WINAPI pe32_win32_set_error_mode(pe32_u32 mode)
{
    (void)mode;
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_set_unhandled_exception_filter(pe32_u32 filter)
{
    pe32_u32 previous = last_filter;
    last_filter = filter;
    return previous;
}

pe32_u32 PE32_WINAPI pe32_win32_unhandled_exception_filter(void *info)
{
    (void)info;
    return 1; /* EXCEPTION_EXECUTE_HANDLER */
}

void PE32_WINAPI pe32_win32_raise_exception(pe32_u32 code, pe32_u32 flags,
                                            pe32_u32 count, const void *args)
{
    (void)flags;
    (void)count;
    (void)args;
    /* No SEH dispatch exists yet: an unhandled exception ends the process. */
    pe32_win32_exit_process(code);
}

void PE32_WINAPI pe32_win32_rtl_unwind(void *frame, void *target,
                                       void *record, pe32_u32 value)
{
    (void)frame;
    (void)target;
    (void)record;
    (void)value;
}

pe32_u32 PE32_WINAPI pe32_win32_virtual_protect(void *address, pe32_u32 size,
                                                pe32_u32 protect,
                                                pe32_u32 *old_protect)
{
    (void)address;
    (void)size;
    (void)protect;
    if (old_protect) {
        *old_protect = 0x40; /* Mappings are already read/write/execute. */
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_free_library(pe32_u32 module)
{
    (void)module;
    return 1; /* Preloaded images are never unloaded. */
}

pe32_u32 PE32_WINAPI pe32_win32_flush_file_buffers(pe32_u32 handle)
{
    (void)handle;
    return 1; /* Only read-only game files are open. */
}

static int event_slot(pe32_u32 handle)
{
    pe32_u32 delta;
    if (handle < EVENT_BASE) {
        return -1;
    }
    delta = handle - EVENT_BASE;
    if (delta % EVENT_STEP || delta / EVENT_STEP >= EVENT_COUNT ||
        !events[delta / EVENT_STEP].active) {
        return -1;
    }
    return (int)(delta / EVENT_STEP);
}

pe32_u32 PE32_WINAPI pe32_win32_create_event_a(const void *security,
                                               pe32_u32 manual_reset,
                                               pe32_u32 initial_state,
                                               const char *name)
{
    unsigned i;
    (void)security;
    (void)name; /* Named events cannot be shared with another process. */
    for (i = 0; i < EVENT_COUNT; i++) {
        if (!events[i].active) {
            events[i].active = 1;
            events[i].manual = manual_reset != 0;
            events[i].signalled = initial_state != 0;
            return EVENT_BASE + i * EVENT_STEP;
        }
    }
    pe32_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_set_event(pe32_u32 handle)
{
    int slot = event_slot(handle);
    if (slot < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    events[slot].signalled = 1;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_wait_for_single_object(pe32_u32 handle,
                                                       pe32_u32 timeout)
{
    int slot = event_slot(handle);
    (void)timeout; /* No other thread could ever signal it. */
    if (slot < 0) {
        pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
        return PE32_WAIT_FAILED;
    }
    if (!events[slot].signalled) {
        return PE32_WAIT_TIMEOUT;
    }
    if (!events[slot].manual) {
        events[slot].signalled = 0;
    }
    return PE32_WAIT_OBJECT_0;
}

int pe32_proc_close_handle(pe32_u32 handle, pe32_u32 *result)
{
    int slot = event_slot(handle);
    if (slot < 0) {
        return 0;
    }
    events[slot].active = 0;
    *result = 1;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_create_thread(const void *security,
                                              pe32_u32 stack, pe32_u32 start,
                                              pe32_u32 parameter,
                                              pe32_u32 flags, pe32_u32 *id)
{
    (void)security;
    (void)stack;
    (void)start;
    (void)parameter;
    (void)flags;
    (void)id;
    pe32_win32_set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_suspend_thread(pe32_u32 thread)
{
    (void)thread;
    return 0xffffffffu;
}

pe32_u32 PE32_WINAPI pe32_win32_resume_thread(pe32_u32 thread)
{
    (void)thread;
    return 0xffffffffu;
}

pe32_u32 PE32_WINAPI pe32_win32_set_thread_priority(pe32_u32 thread,
                                                    pe32_u32 priority)
{
    (void)thread;
    (void)priority;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_duplicate_handle(pe32_u32 src_process,
                                                 pe32_u32 src,
                                                 pe32_u32 dst_process,
                                                 pe32_u32 *dst, pe32_u32 access,
                                                 pe32_u32 inherit,
                                                 pe32_u32 options)
{
    (void)src_process;
    (void)dst_process;
    (void)access;
    (void)inherit;
    (void)options;
    /* Pseudo-handles and events can safely alias; real files cannot. */
    if (dst && (src == pe32_win32_get_current_thread() ||
                src == pe32_win32_get_current_process())) {
        *dst = src;
        return 1;
    }
    pe32_win32_set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

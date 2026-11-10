/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stdlib.h>

#include "pe32_heap.h"
#include "pe32_proc.h"

static void *host_map(pe32_u32 size) { return calloc(1, size); }
static void host_unmap(void *memory, pe32_u32 size) { (void)size; free(memory); }
static pe32_u32 exit_code = 0xffffffffu;
static pe32_u32 slept;
static void host_exit(pe32_u32 code) { exit_code = code; }
static void host_sleep(pe32_u32 ms) { slept += ms; }

int main(void)
{
    unsigned char *block;
    pe32_u32 handle, closed = 0, old = 0;
    pe32_heap_set_backend(host_map, host_unmap);
    pe32_heap_reset();
    pe32_proc_reset();

    block = pe32_win32_global_alloc(PE32_GMEM_ZEROINIT, 64);
    assert(block && block[0] == 0 && block[63] == 0);
    assert(pe32_win32_global_lock(block) == block);
    block = pe32_win32_global_realloc(block, 4096, 0);
    assert(block);
    assert(pe32_win32_global_free(block) == 0);

    handle = pe32_win32_create_event_a(0, 0, 0, 0);
    assert(handle);
    assert(pe32_win32_wait_for_single_object(handle, 0) == PE32_WAIT_TIMEOUT);
    assert(pe32_win32_set_event(handle));
    assert(pe32_win32_wait_for_single_object(handle, 0) == PE32_WAIT_OBJECT_0);
    assert(pe32_win32_wait_for_single_object(handle, 0) == PE32_WAIT_TIMEOUT);
    assert(pe32_proc_close_handle(handle, &closed) && closed == 1);
    assert(pe32_win32_wait_for_single_object(handle, 0) == PE32_WAIT_FAILED);
    assert(!pe32_proc_close_handle(handle, &closed));

    assert(!pe32_win32_create_thread(0, 0, 0, 0, 0, 0));
    assert(pe32_win32_virtual_protect(block, 1, 4, &old) && old);

    pe32_proc_set_backend(host_exit, host_sleep);
    pe32_win32_sleep(30);
    pe32_win32_sleep(12);
    assert(slept == 42);
    (void)exit_code;
    return 0;
}

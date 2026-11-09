/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_HEAP_H
#define A10_PE32_HEAP_H

#include "pe32_win32.h"

/* Page-backed single-threaded private heaps. Memory providers allocate and
 * release whole page-rounded regions; the freestanding i386 runner supplies
 * Linux mmap/munmap, and host tests supply their own provider. */
typedef void *(*pe32_heap_map_fn)(pe32_u32 size);
typedef void (*pe32_heap_unmap_fn)(void *memory, pe32_u32 size);

void pe32_heap_set_backend(pe32_heap_map_fn map, pe32_heap_unmap_fn unmap);
void pe32_heap_reset(void);

void *PE32_WINAPI pe32_win32_heap_create(pe32_u32 flags, pe32_u32 initial,
                                        pe32_u32 maximum);
pe32_u32 PE32_WINAPI pe32_win32_heap_destroy(void *heap);
void *PE32_WINAPI pe32_win32_heap_alloc(void *heap, pe32_u32 flags,
                                       pe32_u32 bytes);
void *PE32_WINAPI pe32_win32_heap_realloc(void *heap, pe32_u32 flags,
                                         void *memory, pe32_u32 bytes);
pe32_u32 PE32_WINAPI pe32_win32_heap_free(void *heap, pe32_u32 flags,
                                         void *memory);
pe32_u32 PE32_WINAPI pe32_win32_heap_size(void *heap, pe32_u32 flags,
                                         const void *memory);

#endif

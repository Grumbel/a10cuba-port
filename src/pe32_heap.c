/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Freestanding, single-thread Win32 private heaps. The native backend maps
 * pages once per segment (rather than on each small allocation), and free
 * blocks split/coalesce for reuse. Only implemented Win32 flags are accepted.
 * In particular, structured exceptions and executable heaps are NOT faked. */

#include "pe32_heap.h"

enum {
    PAGE_SIZE = 4096,
    SEGMENT_SIZE = 65536,
    BLOCK_ALIGNMENT = 8,
    ERROR_NOT_ENOUGH_MEMORY = 8,
    ERROR_INVALID_HANDLE = 6,
    ERROR_INVALID_PARAMETER = 87,
    HEAP_NO_SERIALIZE = 0x1,
    HEAP_GENERATE_EXCEPTIONS = 0x4,
    HEAP_ZERO_MEMORY = 0x8,
    HEAP_REALLOC_IN_PLACE_ONLY = 0x10,
    HEAP_CREATE_ENABLE_EXECUTE = 0x40000,
    FIXED_HEAP_MAX_BLOCK = 0x7fff8
};

struct heap_block {
    struct heap_block *next;
    pe32_u32 capacity;
    pe32_u32 requested;
    pe32_u32 in_use;
};

struct heap_segment {
    struct heap_segment *next;
    pe32_u32 size;
};

struct private_heap {
    struct private_heap *next;
    struct heap_segment *segments;
    pe32_u32 maximum;
    pe32_u32 mapped_bytes;
};

static struct private_heap *heaps;
static pe32_heap_map_fn map_pages;
static pe32_heap_unmap_fn unmap_pages;

/* Both headers must end on an 8-byte boundary on i386 and the test host. */
typedef char block_alignment_check[(sizeof(struct heap_block) % 8) ? -1 : 1];
typedef char segment_alignment_check[(sizeof(struct heap_segment) % 8) ? -1 : 1];

static void copy_bytes(void *dst, const void *src, pe32_u32 count)
{
    pe32_u32 i;
    unsigned char *to = (unsigned char *)dst;
    const unsigned char *from = (const unsigned char *)src;
    for (i = 0; i < count; ++i) {
        to[i] = from[i];
    }
}

static void clear_bytes(void *memory, pe32_u32 count)
{
    pe32_u32 i;
    unsigned char *bytes = (unsigned char *)memory;
    for (i = 0; i < count; ++i) {
        bytes[i] = 0;
    }
}

static int round_up(pe32_u32 value, pe32_u32 multiple, pe32_u32 *result)
{
    if (value > 0xffffffffu - (multiple - 1)) {
        return 0;
    }
    *result = (value + multiple - 1) & ~(multiple - 1);
    return 1;
}

static struct private_heap *find_heap(void *handle)
{
    struct private_heap *heap;
    for (heap = heaps; heap; heap = heap->next) {
        if ((void *)heap == handle) {
            return heap;
        }
    }
    return (struct private_heap *)0;
}

static void release_heap(struct private_heap *heap)
{
    struct heap_segment *segment = heap->segments;
    while (segment) {
        struct heap_segment *next = segment->next;
        unmap_pages(segment, segment->size);
        segment = next;
    }
    unmap_pages(heap, PAGE_SIZE);
}

void pe32_heap_reset(void)
{
    struct private_heap *heap = heaps;
    while (heap) {
        struct private_heap *next = heap->next;
        release_heap(heap);
        heap = next;
    }
    heaps = (struct private_heap *)0;
}

void pe32_heap_set_backend(pe32_heap_map_fn map, pe32_heap_unmap_fn unmap)
{
    if (unmap_pages) {
        pe32_heap_reset();
    }
    map_pages = map;
    unmap_pages = unmap;
}

static struct heap_segment *add_segment(struct private_heap *heap,
                                        pe32_u32 minimum_bytes)
{
    struct heap_segment *segment;
    struct heap_block *block;
    pe32_u32 need, size, limit;

    if (minimum_bytes > 0xffffffffu - (pe32_u32)sizeof(*segment) -
            (pe32_u32)sizeof(*block)) {
        return (struct heap_segment *)0;
    }
    need = minimum_bytes + (pe32_u32)sizeof(*segment) +
           (pe32_u32)sizeof(*block);
    if (!round_up(need, PAGE_SIZE, &need)) {
        return (struct heap_segment *)0;
    }
    size = need > SEGMENT_SIZE ? need : SEGMENT_SIZE;
    if (heap->maximum) {
        if (!round_up(heap->maximum, PAGE_SIZE, &limit) ||
            heap->mapped_bytes > limit || need > limit - heap->mapped_bytes) {
            return (struct heap_segment *)0;
        }
        if (size > limit - heap->mapped_bytes) {
            size = limit - heap->mapped_bytes;
        }
    }
    if (!size || !map_pages || !unmap_pages) {
        return (struct heap_segment *)0;
    }
    segment = (struct heap_segment *)map_pages(size);
    if (!segment) {
        return (struct heap_segment *)0;
    }
    segment->size = size;
    segment->next = heap->segments;
    heap->segments = segment;
    heap->mapped_bytes += size;
    block = (struct heap_block *)(segment + 1);
    block->next = (struct heap_block *)0;
    block->capacity = size - (pe32_u32)sizeof(*segment) -
                      (pe32_u32)sizeof(*block);
    block->requested = 0;
    block->in_use = 0;
    return segment;
}

static struct heap_block *first_block(struct heap_segment *segment)
{
    return (struct heap_block *)(segment + 1);
}

static void *block_data(struct heap_block *block)
{
    return (void *)(block + 1);
}

static struct heap_block *find_block(struct private_heap *heap,
                                     const void *memory)
{
    struct heap_segment *segment;
    for (segment = heap->segments; segment; segment = segment->next) {
        struct heap_block *block;
        for (block = first_block(segment); block; block = block->next) {
            if (block_data(block) == memory) {
                return block;
            }
        }
    }
    return (struct heap_block *)0;
}

static void split_block(struct heap_block *block, pe32_u32 capacity)
{
    if (block->capacity - capacity >= sizeof(*block) + BLOCK_ALIGNMENT) {
        struct heap_block *tail = (struct heap_block *)(
            (unsigned char *)block_data(block) + capacity);
        tail->next = block->next;
        tail->capacity = block->capacity - capacity - (pe32_u32)sizeof(*block);
        tail->requested = 0;
        tail->in_use = 0;
        block->next = tail;
        block->capacity = capacity;
    }
}

static void merge_forward(struct heap_block *block)
{
    while (block->next && !block->next->in_use) {
        block->capacity += (pe32_u32)sizeof(*block) + block->next->capacity;
        block->next = block->next->next;
    }
}

static struct heap_block *find_free_block(struct private_heap *heap,
                                          pe32_u32 capacity)
{
    struct heap_segment *segment;
    for (segment = heap->segments; segment; segment = segment->next) {
        struct heap_block *block;
        for (block = first_block(segment); block; block = block->next) {
            if (!block->in_use && block->capacity >= capacity) {
                return block;
            }
        }
    }
    return (struct heap_block *)0;
}

void *PE32_WINAPI pe32_win32_heap_create(pe32_u32 flags, pe32_u32 initial,
                                        pe32_u32 maximum)
{
    struct private_heap *heap;
    if (!map_pages || !unmap_pages ||
        (flags & ~(pe32_u32)HEAP_NO_SERIALIZE) ||
        (maximum && initial >= maximum)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return (void *)0;
    }
    heap = (struct private_heap *)map_pages(PAGE_SIZE);
    if (!heap) {
        pe32_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return (void *)0;
    }
    heap->segments = (struct heap_segment *)0;
    heap->maximum = maximum;
    heap->mapped_bytes = 0;
    heap->next = heaps;
    heaps = heap;
    /* The initial reserve/commit model is approximated with segments.
     * Do not allocate multi-megabyte initial sizes before actual use. */
    (void)initial;
    return heap;
}

pe32_u32 PE32_WINAPI pe32_win32_heap_destroy(void *handle)
{
    struct private_heap **cursor;
    for (cursor = &heaps; *cursor; cursor = &(*cursor)->next) {
        if (*cursor == handle) {
            struct private_heap *heap = *cursor;
            *cursor = heap->next;
            release_heap(heap);
            return 1;
        }
    }
    pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
    return 0;
}

void *PE32_WINAPI pe32_win32_heap_alloc(void *handle, pe32_u32 flags,
                                       pe32_u32 bytes)
{
    struct private_heap *heap = find_heap(handle);
    struct heap_block *block;
    pe32_u32 capacity;
    /* HeapAlloc does not set last error on failure. Nor can this bootstrap
     * implement Windows structured exceptions for GENERATE_EXCEPTIONS. */
    if (!heap || (flags & ~(pe32_u32)(HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY)) ||
        (heap->maximum && bytes >= FIXED_HEAP_MAX_BLOCK) ||
        !round_up(bytes ? bytes : 1, BLOCK_ALIGNMENT, &capacity)) {
        return (void *)0;
    }
    block = find_free_block(heap, capacity);
    if (!block) {
        struct heap_segment *segment = add_segment(heap, capacity);
        if (!segment) {
            return (void *)0;
        }
        block = first_block(segment);
    }
    split_block(block, capacity);
    block->in_use = 1;
    block->requested = bytes;
    if (flags & HEAP_ZERO_MEMORY) {
        clear_bytes(block_data(block), bytes);
    }
    return block_data(block);
}

pe32_u32 PE32_WINAPI pe32_win32_heap_free(void *handle, pe32_u32 flags,
                                         void *memory)
{
    struct private_heap *heap = find_heap(handle);
    struct heap_block *block, *previous = (struct heap_block *)0;
    struct heap_segment *segment;
    if (!heap || (flags & ~(pe32_u32)HEAP_NO_SERIALIZE)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!memory) {
        return 1;
    }
    for (segment = heap->segments; segment; segment = segment->next) {
        previous = (struct heap_block *)0;
        for (block = first_block(segment); block; block = block->next) {
            if (block_data(block) == memory) {
                if (!block->in_use) {
                    pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
                    return 0;
                }
                block->in_use = 0;
                block->requested = 0;
                merge_forward(block);
                if (previous && !previous->in_use) {
                    merge_forward(previous);
                }
                return 1;
            }
            previous = block;
        }
    }
    pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_heap_size(void *handle, pe32_u32 flags,
                                         const void *memory)
{
    struct private_heap *heap = find_heap(handle);
    struct heap_block *block;
    if (!heap || (flags & ~(pe32_u32)HEAP_NO_SERIALIZE) || !memory) {
        return 0xffffffffu;
    }
    block = find_block(heap, memory);
    /* Win32 HeapSize returns the requested allocation length, not capacity,
     * and does not set last error on failure. */
    return block && block->in_use ? block->requested : 0xffffffffu;
}

void *PE32_WINAPI pe32_win32_heap_realloc(void *handle, pe32_u32 flags,
                                         void *memory, pe32_u32 bytes)
{
    struct private_heap *heap = find_heap(handle);
    struct heap_block *block;
    pe32_u32 capacity, old_size;
    void *replacement;
    if (!heap || !memory ||
        (flags & ~(pe32_u32)(HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY |
                             HEAP_REALLOC_IN_PLACE_ONLY)) ||
        (heap->maximum && bytes >= FIXED_HEAP_MAX_BLOCK) ||
        !round_up(bytes ? bytes : 1, BLOCK_ALIGNMENT, &capacity)) {
        return (void *)0;
    }
    block = find_block(heap, memory);
    if (!block || !block->in_use) {
        return (void *)0;
    }
    old_size = block->requested;
    if (capacity > block->capacity) {
        merge_forward(block); /* Only free immediately following blocks. */
    }
    if (capacity <= block->capacity) {
        split_block(block, capacity);
        if ((flags & HEAP_ZERO_MEMORY) && bytes > old_size) {
            clear_bytes((unsigned char *)memory + old_size, bytes - old_size);
        }
        block->requested = bytes;
        return memory;
    }
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY) {
        return (void *)0;
    }
    replacement = pe32_win32_heap_alloc(handle, flags & HEAP_ZERO_MEMORY, bytes);
    if (!replacement) {
        return (void *)0;
    }
    copy_bytes(replacement, memory, old_size < bytes ? old_size : bytes);
    if ((flags & HEAP_ZERO_MEMORY) && bytes > old_size) {
        clear_bytes((unsigned char *)replacement + old_size, bytes - old_size);
    }
    (void)pe32_win32_heap_free(handle, 0, memory);
    return replacement;
}

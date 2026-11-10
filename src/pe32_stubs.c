/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_stubs.h"

struct stub_entry {
    const char *dll;
    const char *name;
    pe32_u32 arguments;
    pe32_u32 result;
    pe32_u32 reported;
};

static struct stub_entry stubs[] = {
#define X(dll, name, args, ret) { dll, name, args, ret, 0 },
#include "pe32_stubs.def"
#undef X
};

#define STUB_COUNT (sizeof stubs / sizeof stubs[0])

static pe32_stub_log_fn logger;

void pe32_stubs_set_logger(pe32_stub_log_fn fn) { logger = fn; }

void pe32_stubs_report(const char *dll, const char *name)
{
    if (logger) {
        logger(dll, name);
    }
}

pe32_u32 pe32_stubs_count(void) { return (pe32_u32)STUB_COUNT; }

const char *pe32_stubs_name(pe32_u32 index, const char **dll,
                            pe32_u32 *arguments)
{
    if (index >= STUB_COUNT) {
        return (const char *)0;
    }
    if (dll) {
        *dll = stubs[index].dll;
    }
    if (arguments) {
        *arguments = stubs[index].arguments;
    }
    return stubs[index].name;
}

/* Called from the i386 trampolines with the table index (cdecl). */
pe32_u32 pe32_stub_dispatch(pe32_u32 index)
{
    if (index >= STUB_COUNT) {
        return 0;
    }
    if (!stubs[index].reported && logger) {
        stubs[index].reported = 1;
        logger(stubs[index].dll, stubs[index].name);
    }
    return stubs[index].result;
}

static int equal_fold(const char *a, const char *b)
{
    while (*a && ((*a | 32) == (*b | 32))) {
        a++;
        b++;
    }
    return !*a && !*b;
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void format_ordinal(pe32_u32 ordinal, char *out)
{
    char digits[11];
    int n = 0, i = 0;
    do {
        digits[n++] = (char)('0' + ordinal % 10);
        ordinal /= 10;
    } while (ordinal);
    out[i++] = '#';
    while (n) {
        out[i++] = digits[--n];
    }
    out[i] = 0;
}

#if defined(__i386__)
extern char pe32_stub_base[];
#endif

enum pe32_link_status pe32_stubs_resolve(const char *dll, const char *name,
                                         pe32_u32 ordinal, pe32_u32 *address)
{
    char key[16];
    pe32_u32 i;
    if (!dll || !address) {
        return PE32_LINK_MISSING;
    }
    if (!name) {
        format_ordinal(ordinal, key);
        name = key;
    }
    for (i = 0; i < STUB_COUNT; i++) {
        if (equal_fold(dll, stubs[i].dll) && equal(name, stubs[i].name)) {
#if defined(__i386__)
            *address = (pe32_u32)(unsigned long)(pe32_stub_base + 16u * i);
            return PE32_LINK_OK;
#else
            return PE32_LINK_MISSING; /* Trampolines are i386 machine code. */
#endif
        }
    }
    return PE32_LINK_MISSING;
}

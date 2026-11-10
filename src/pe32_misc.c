/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_misc.h"

enum {
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_CALL_NOT_IMPLEMENTED = 120,
    ERROR_INSUFFICIENT_BUFFER = 122,
    ERROR_INVALID_HANDLE = 6,
    ERROR_NOT_ENOUGH_MEMORY = 8,
    OF_PARSE = 0x100,
    OF_EXIST = 0x4000,
    OF_ACCESS_MASK = 3,
    HFILE_ERROR = -1,
    ATOM_COUNT = 64,
    ATOM_BASE = 0xc000,
    ATOM_NAME_MAX = 255
};

typedef unsigned long long u64;

/* Shift-subtract 64/32 division: avoids libgcc's __udivdi3 in the runner. */
static u64 divide_u64(u64 value, pe32_u32 divisor, pe32_u32 *remainder)
{
    u64 quotient = 0, rest = 0;
    int bit;
    for (bit = 63; bit >= 0; bit--) {
        rest = (rest << 1) | ((value >> bit) & 1u);
        if (rest >= divisor) {
            rest -= divisor;
            quotient |= (u64)1 << bit;
        }
    }
    *remainder = (pe32_u32)rest;
    return quotient;
}

static u64 ticks_of(const struct pe32_filetime *t)
{
    return ((u64)t->high << 32) | t->low;
}

pe32_u32 PE32_WINAPI pe32_win32_file_time_to_local_file_time(
    const struct pe32_filetime *utc, struct pe32_filetime *local)
{
    if (!utc || !local) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    *local = *utc; /* The guest time zone is UTC. */
    return 1;
}

static int is_leap(pe32_u32 year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

pe32_u32 PE32_WINAPI pe32_win32_file_time_to_system_time(
    const struct pe32_filetime *file_time, struct pe32_system_time *system)
{
    static const pe32_u8 days_in_month[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    pe32_u32 sub, seconds_of_day, days, year = 1601, month = 0, length;
    u64 total, whole_seconds;
    if (!file_time || !system || (file_time->high & 0x80000000u)) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    total = ticks_of(file_time);
    whole_seconds = divide_u64(total, 10000000u, &sub);
    system->milliseconds = (pe32_u16)(sub / 10000u);
    whole_seconds = divide_u64(whole_seconds, 86400u, &seconds_of_day);
    days = (pe32_u32)whole_seconds; /* Days since 1601-01-01 (a Monday). */
    system->day_of_week = (pe32_u16)((days + 1) % 7);
    system->hour = (pe32_u16)(seconds_of_day / 3600u);
    system->minute = (pe32_u16)(seconds_of_day / 60u % 60u);
    system->second = (pe32_u16)(seconds_of_day % 60u);
    for (;;) {
        length = is_leap(year) ? 366u : 365u;
        if (days < length) {
            break;
        }
        days -= length;
        year++;
    }
    for (;;) {
        length = days_in_month[month] + (month == 1 && is_leap(year));
        if (days < length) {
            break;
        }
        days -= length;
        month++;
    }
    system->year = (pe32_u16)year;
    system->month = (pe32_u16)(month + 1);
    system->day = (pe32_u16)(days + 1);
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_get_time_zone_information(void *information)
{
    pe32_u8 *bytes = information;
    unsigned i;
    if (information) {
        for (i = 0; i < 172; i++) { /* TIME_ZONE_INFORMATION is 172 bytes. */
            bytes[i] = 0;
        }
        bytes[4] = 'U'; /* StandardName (UTF-16) = "UTC" */
        bytes[6] = 'T';
        bytes[8] = 'C';
    }
    return 0; /* TIME_ZONE_ID_UNKNOWN: no daylight-saving rules. */
}

static void copy_path(char *target, const char *source, unsigned capacity)
{
    unsigned i = 0;
    while (source[i] && i + 1 < capacity) {
        target[i] = source[i];
        i++;
    }
    target[i] = 0;
}

pe32_u32 PE32_WINAPI pe32_win32_open_file(const char *name, void *of_struct,
                                          pe32_u32 style)
{
    pe32_u8 *record = of_struct;
    pe32_u32 handle;
    unsigned i;
    if (!name || !record) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return (pe32_u32)HFILE_ERROR;
    }
    for (i = 0; i < PE32_OFSTRUCT_SIZE; i++) {
        record[i] = 0;
    }
    record[0] = PE32_OFSTRUCT_SIZE;
    copy_path((char *)record + 8, name, 128);
    if ((style & OF_ACCESS_MASK) != 0 && !(style & (OF_EXIST | OF_PARSE))) {
        pe32_win32_set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
        record[2] = ERROR_CALL_NOT_IMPLEMENTED;
        return (pe32_u32)HFILE_ERROR;
    }
    if (style & OF_PARSE) {
        return 0;
    }
    handle = pe32_win32_create_file_a(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (handle == 0xffffffffu) {
        record[2] = ERROR_FILE_NOT_FOUND;
        return (pe32_u32)HFILE_ERROR;
    }
    if (style & OF_EXIST) {
        (void)pe32_win32_close_handle(handle);
        return 1;
    }
    return handle;
}

pe32_u32 PE32_WINAPI pe32_win32__lclose(pe32_u32 handle)
{
    return pe32_win32_close_handle(handle) ? 0 : (pe32_u32)HFILE_ERROR;
}

pe32_u32 PE32_WINAPI pe32_win32__llseek(pe32_u32 handle, pe32_u32 offset,
                                        pe32_u32 origin)
{
    return pe32_win32_set_file_pointer(handle, offset, 0, origin);
}

static struct {
    char name[ATOM_NAME_MAX + 1];
    pe32_u32 references;
} atoms[ATOM_COUNT];

static char fold(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same_atom(const char *a, const char *b)
{
    while (*a && fold(*a) == fold(*b)) {
        a++;
        b++;
    }
    return fold(*a) == fold(*b);
}

pe32_u32 PE32_WINAPI pe32_win32_global_add_atom_a(const char *name)
{
    int i, free_slot = -1;
    if (!name || !name[0]) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((unsigned long)name <= 0xfffful) {
        return (pe32_u32)(unsigned long)name; /* MAKEINTATOM */
    }
    for (i = 0; i < ATOM_COUNT; i++) {
        if (!atoms[i].references) {
            if (free_slot < 0) {
                free_slot = i;
            }
        } else if (same_atom(atoms[i].name, name)) {
            atoms[i].references++;
            return ATOM_BASE + (pe32_u32)i;
        }
    }
    for (i = 0; name[i]; i++) {
        if (i >= ATOM_NAME_MAX) {
            pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
            return 0;
        }
    }
    if (free_slot < 0) {
        pe32_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    copy_path(atoms[free_slot].name, name, ATOM_NAME_MAX + 1);
    atoms[free_slot].references = 1;
    return ATOM_BASE + (pe32_u32)free_slot;
}

pe32_u32 PE32_WINAPI pe32_win32_global_delete_atom(pe32_u32 atom)
{
    if (atom < ATOM_BASE || atom >= ATOM_BASE + ATOM_COUNT ||
        !atoms[atom - ATOM_BASE].references) {
        return atom; /* Failure returns the atom, success returns 0. */
    }
    atoms[atom - ATOM_BASE].references--;
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_global_get_atom_name_a(pe32_u32 atom,
                                                       char *buffer, int size)
{
    pe32_u32 length = 0;
    if (atom < ATOM_BASE || atom >= ATOM_BASE + ATOM_COUNT ||
        !atoms[atom - ATOM_BASE].references || !buffer || size <= 0) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    while (atoms[atom - ATOM_BASE].name[length]) {
        length++;
    }
    if (length >= (pe32_u32)size) {
        length = (pe32_u32)size - 1; /* Windows truncates. */
    }
    for (atom -= ATOM_BASE, size = 0; (pe32_u32)size < length; size++) {
        buffer[size] = atoms[atom].name[size];
    }
    buffer[length] = 0;
    return length;
}

pe32_u32 PE32_WINAPI pe32_win32_write_private_profile_string_a(
    const char *section, const char *key, const char *value, const char *file)
{
    (void)section; (void)key; (void)value; (void)file;
    return 1; /* Accepted and discarded: user state is not persisted yet. */
}

pe32_u32 PE32_WINAPI pe32_win32_set_environment_variable_a(const char *name,
                                                           const char *value)
{
    (void)name; (void)value;
    return 1; /* The guest environment is empty and write-only. */
}

pe32_u32 PE32_WINAPI pe32_win32_get_process_version(pe32_u32 process_id)
{
    (void)process_id;
    return 0x00040000u; /* Subsystem version 4.0. */
}

pe32_u32 PE32_WINAPI pe32_win32_win_exec(const char *command, pe32_u32 show)
{
    (void)command; (void)show;
    return 2; /* ERROR_FILE_NOT_FOUND: values below 32 are errors. */
}

pe32_u32 PE32_WINAPI pe32_win32_get_volume_information_a(
    const char *root, char *name, pe32_u32 name_size, pe32_u32 *serial,
    pe32_u32 *max_component, pe32_u32 *flags, char *fs_name,
    pe32_u32 fs_name_size)
{
    (void)root;
    if (name && name_size) {
        copy_path(name, "A10CUBA", name_size);
    }
    if (serial) {
        *serial = 0;
    }
    if (max_component) {
        *max_component = 255;
    }
    if (flags) {
        *flags = 0;
    }
    if (fs_name && fs_name_size) {
        copy_path(fs_name, "NTFS", fs_name_size);
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_lock_file(pe32_u32 h, pe32_u32 off_lo,
                                          pe32_u32 off_hi, pe32_u32 len_lo,
                                          pe32_u32 len_hi)
{
    (void)h; (void)off_lo; (void)off_hi; (void)len_lo; (void)len_hi;
    return 1; /* Single process, read-only files: locks always succeed. */
}

pe32_u32 PE32_WINAPI pe32_win32_set_end_of_file(pe32_u32 handle)
{
    (void)handle;
    pe32_win32_set_last_error(5); /* ERROR_ACCESS_DENIED: files are read-only */
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_peek_named_pipe(pe32_u32 h, void *buf,
                                                pe32_u32 size, pe32_u32 *read,
                                                pe32_u32 *avail,
                                                pe32_u32 *left)
{
    (void)h; (void)buf; (void)size; (void)read; (void)avail; (void)left;
    pe32_win32_set_last_error(ERROR_INVALID_HANDLE);
    return 0;
}

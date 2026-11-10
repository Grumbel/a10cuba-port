/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_MISC_H
#define A10_PE32_MISC_H

#include "pe32_io.h"

/* Time conversion (the guest time zone is always UTC), legacy 16-bit-style
 * file calls layered on CreateFileA, global atoms and inert configuration
 * calls. Freestanding: no libc and no 64-bit libgcc division. */
struct pe32_system_time {
    pe32_u16 year, month, day_of_week, day, hour, minute, second, milliseconds;
};

pe32_u32 PE32_WINAPI pe32_win32_file_time_to_local_file_time(
    const struct pe32_filetime *utc, struct pe32_filetime *local);
pe32_u32 PE32_WINAPI pe32_win32_file_time_to_system_time(
    const struct pe32_filetime *file_time, struct pe32_system_time *system);
pe32_u32 PE32_WINAPI pe32_win32_get_time_zone_information(void *information);

/* OFSTRUCT is 136 bytes; only OF_READ, OF_EXIST and OF_PARSE are supported. */
#define PE32_OFSTRUCT_SIZE 136u
pe32_u32 PE32_WINAPI pe32_win32_open_file(const char *name, void *of_struct,
                                          pe32_u32 style);
pe32_u32 PE32_WINAPI pe32_win32__lclose(pe32_u32 handle);
pe32_u32 PE32_WINAPI pe32_win32__llseek(pe32_u32 handle, pe32_u32 offset,
                                        pe32_u32 origin);

/* Global atoms: case-insensitive, reference-counted ANSI strings. */
pe32_u32 PE32_WINAPI pe32_win32_global_add_atom_a(const char *name);
pe32_u32 PE32_WINAPI pe32_win32_global_delete_atom(pe32_u32 atom);
pe32_u32 PE32_WINAPI pe32_win32_global_get_atom_name_a(pe32_u32 atom,
                                                       char *buffer, int size);

/* Configuration and environment calls. Writes are accepted and discarded
 * (the game never reads them back from a Windows registry/INI here). */
pe32_u32 PE32_WINAPI pe32_win32_write_private_profile_string_a(
    const char *section, const char *key, const char *value, const char *file);
pe32_u32 PE32_WINAPI pe32_win32_set_environment_variable_a(const char *name,
                                                           const char *value);
pe32_u32 PE32_WINAPI pe32_win32_get_process_version(pe32_u32 process_id);
pe32_u32 PE32_WINAPI pe32_win32_win_exec(const char *command, pe32_u32 show);
pe32_u32 PE32_WINAPI pe32_win32_get_volume_information_a(
    const char *root, char *name, pe32_u32 name_size, pe32_u32 *serial,
    pe32_u32 *max_component, pe32_u32 *flags, char *fs_name,
    pe32_u32 fs_name_size);
pe32_u32 PE32_WINAPI pe32_win32_lock_file(pe32_u32 h, pe32_u32 off_lo,
                                          pe32_u32 off_hi, pe32_u32 len_lo,
                                          pe32_u32 len_hi);
pe32_u32 PE32_WINAPI pe32_win32_set_end_of_file(pe32_u32 handle);
pe32_u32 PE32_WINAPI pe32_win32_peek_named_pipe(pe32_u32 h, void *buf,
                                                pe32_u32 size, pe32_u32 *read,
                                                pe32_u32 *avail,
                                                pe32_u32 *left);

#endif

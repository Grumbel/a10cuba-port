/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_TEXT_H
#define A10_PE32_TEXT_H

#include "pe32_win32.h"

/* Freestanding KERNEL32 string, code page, locale and version shims.
 * Limits (details in pe32_text.c):
 *  - The ANSI code page is treated as 1252 and OEM as 437, but BOTH are
 *    mapped as Latin-1 (one byte per character). Real 1252 0x80-0x9F glyphs
 *    and the IBM 437 glyph set are NOT implemented.
 *  - Comparison, case mapping and character classes are ordinal and
 *    ASCII-only. There is no linguistic collation.
 *  - The locale argument is ignored; GetLocaleInfo knows en-US values only.
 * Failures set the single-thread last error via pe32_win32_set_last_error.
 * Pointer arguments are not probed; callers must pass valid buffers. */

/* Character type bits for GetStringType (CT_CTYPE1 only). */
#define PE32_CT_CTYPE1 0x00000001u
#define PE32_C1_UPPER 0x0001u
#define PE32_C1_LOWER 0x0002u
#define PE32_C1_DIGIT 0x0004u
#define PE32_C1_SPACE 0x0008u
#define PE32_C1_PUNCT 0x0010u
#define PE32_C1_CNTRL 0x0020u
#define PE32_C1_BLANK 0x0040u
#define PE32_C1_XDIGIT 0x0080u
#define PE32_C1_ALPHA 0x0100u

/* LCMapString flags and CompareString results. */
#define PE32_LCMAP_LOWERCASE 0x00000100u
#define PE32_LCMAP_UPPERCASE 0x00000200u
#define PE32_NORM_IGNORECASE 0x00000001u
#define PE32_CSTR_LESS_THAN 1
#define PE32_CSTR_EQUAL 2
#define PE32_CSTR_GREATER_THAN 3

int PE32_WINAPI pe32_win32_lstrcmp_a(const char *string1, const char *string2);
int PE32_WINAPI pe32_win32_lstrcmpi_a(const char *string1, const char *string2);

/* Rounds |number * numerator / denominator| half away from zero, then
 * applies the sign. Returns -1 on a zero denominator or int overflow. */
int PE32_WINAPI pe32_win32_mul_div(int number, int numerator, int denominator);

/* Stubs: 0 when size is 0, 1 when the pointer is NULL, otherwise 0 without
 * probing memory. Real access validation is not implemented. */
int PE32_WINAPI pe32_win32_is_bad_read_ptr(const void *pointer, pe32_u32 size);
int PE32_WINAPI pe32_win32_is_bad_write_ptr(void *pointer, pe32_u32 size);
int PE32_WINAPI pe32_win32_is_bad_code_ptr(const void *pointer);

pe32_u32 PE32_WINAPI pe32_win32_get_acp(void);
pe32_u32 PE32_WINAPI pe32_win32_get_oemcp(void);

/* Accepts CP_ACP (0), CP_OEMCP (1), CP_THREAD_ACP (3), 1252 and 437. Fills a
 * 18-byte CPINFO: UINT MaxCharSize (1), BYTE DefaultChar[2] ('?', 0) and
 * BYTE LeadByte[12] (all zero). Any other code page fails with 87. */
int PE32_WINAPI pe32_win32_get_cp_info(pe32_u32 code_page, pe32_u8 *cp_info);

/* Reports 0xC0000004u: Windows 9x high bit, major 4, minor 0x0A (4.10). */
pe32_u32 PE32_WINAPI pe32_win32_get_version(void);

int PE32_WINAPI pe32_win32_multi_byte_to_wide_char(pe32_u32 code_page,
                                                  pe32_u32 flags,
                                                  const char *source,
                                                  int source_length,
                                                  pe32_u16 *dest,
                                                  int dest_length);
int PE32_WINAPI pe32_win32_wide_char_to_multi_byte(pe32_u32 code_page,
                                                  pe32_u32 flags,
                                                  const pe32_u16 *source,
                                                  int source_length,
                                                  char *dest,
                                                  int dest_length,
                                                  const char *default_char,
                                                  int *used_default);

int PE32_WINAPI pe32_win32_get_string_type_a(pe32_u32 locale,
                                            pe32_u32 info_type,
                                            const char *source, int count,
                                            pe32_u16 *char_types);
int PE32_WINAPI pe32_win32_get_string_type_w(pe32_u32 info_type,
                                            const pe32_u16 *source, int count,
                                            pe32_u16 *char_types);

int PE32_WINAPI pe32_win32_lc_map_string_a(pe32_u32 locale, pe32_u32 flags,
                                          const char *source, int source_length,
                                          char *dest, int dest_length);
int PE32_WINAPI pe32_win32_lc_map_string_w(pe32_u32 locale, pe32_u32 flags,
                                          const pe32_u16 *source,
                                          int source_length, pe32_u16 *dest,
                                          int dest_length);

int PE32_WINAPI pe32_win32_compare_string_a(pe32_u32 locale, pe32_u32 flags,
                                           const char *string1, int length1,
                                           const char *string2, int length2);
int PE32_WINAPI pe32_win32_compare_string_w(pe32_u32 locale, pe32_u32 flags,
                                           const pe32_u16 *string1,
                                           int length1,
                                           const pe32_u16 *string2,
                                           int length2);

/* Supports LOCALE_ILANGUAGE (0x1, "0409"), LOCALE_SLANGUAGE (0x2, "English
 * (United States)"), LOCALE_SDECIMAL (0xE, ".") and LOCALE_STHOUSAND (0xF,
 * ","). Other types, and LOCALE_RETURN_NUMBER, fail with 1004. */
int PE32_WINAPI pe32_win32_get_locale_info_a(pe32_u32 locale,
                                            pe32_u32 lc_type, char *buffer,
                                            int capacity);
int PE32_WINAPI pe32_win32_get_locale_info_w(pe32_u32 locale,
                                            pe32_u32 lc_type, pe32_u16 *buffer,
                                            int capacity);

#endif

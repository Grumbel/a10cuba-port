/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_STUBS_H
#define A10_PE32_STUBS_H

#include "pe32_link.h"

/* Traced do-nothing replacements for imports that have no implementation yet
 * (see pe32_stubs.def). Each function is reported once through the logger,
 * then every call returns the table's default value. */
typedef void (*pe32_stub_log_fn)(const char *dll, const char *name);
void pe32_stubs_set_logger(pe32_stub_log_fn logger);

pe32_u32 pe32_stubs_count(void);
const char *pe32_stubs_name(pe32_u32 index, const char **dll,
                            pe32_u32 *arguments);

/* name == NULL selects an ordinal import. */
enum pe32_link_status pe32_stubs_resolve(const char *dll, const char *name,
                                         pe32_u32 ordinal, pe32_u32 *address);

#endif

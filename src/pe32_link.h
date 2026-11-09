/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_LINK_H
#define A10_PE32_LINK_H

/* No libc dependency: this is also compiled into the freestanding i386 ELF. */
typedef unsigned char pe32_u8;
typedef unsigned short pe32_u16;
typedef unsigned int pe32_u32;

struct pe32_module {
    pe32_u8 *image;
    pe32_u32 size;
    pe32_u32 base;
    pe32_u32 entry_rva;
    pe32_u32 export_rva;
    pe32_u32 export_size;
    pe32_u32 import_rva;
    pe32_u32 import_size;
    pe32_u32 tls_rva;
    pe32_u32 tls_size;
    const char *path;
};

enum pe32_link_status {
    PE32_LINK_OK = 0,
    PE32_LINK_MISSING = 1,
    PE32_LINK_FORWARDER = 2,
    PE32_LINK_INVALID = 3
};

typedef void (*pe32_link_report)(void *context, const char *dll,
                                  const char *name, pe32_u32 ordinal,
                                  enum pe32_link_status status,
                                  pe32_u32 address);

/* name != NULL selects exact, case-sensitive named lookup; otherwise ordinal. */
enum pe32_link_status pe32_find_export(const struct pe32_module *module,
                                        const char *name, pe32_u32 ordinal,
                                        pe32_u32 *address);

/* Resolve names from the module's PE export directory (Windows DLL name). */
const char *pe32_export_module_name(const struct pe32_module *module);

/* First validates all imports, then writes IAT entries. On any error the
 * target's IAT remains untouched. OS DLLs are deliberately NOT stubbed. */
enum pe32_link_status pe32_bind_imports(struct pe32_module *target,
                                        const struct pe32_module *modules,
                                        pe32_u32 module_count,
                                        pe32_link_report report,
                                        void *context,
                                        pe32_u32 *linked_count);

#endif

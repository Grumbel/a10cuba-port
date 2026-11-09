/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* PE32 import/export resolution, shared by Linux/i386 and host tests.
 * No host pointers are written into PE data: all addresses are 32-bit VAs.
 * This does not implement delay imports, API sets or forwarded exports. */

#include "pe32_link.h"

static int contains(pe32_u32 size, pe32_u32 offset, pe32_u32 length)
{
    return offset <= size && length <= size - offset;
}

static pe32_u16 read16(const pe32_u8 *p)
{
    return (pe32_u16)(p[0] | ((pe32_u16)p[1] << 8));
}

static pe32_u32 read32(const pe32_u8 *p)
{
    return (pe32_u32)p[0] | ((pe32_u32)p[1] << 8) |
           ((pe32_u32)p[2] << 16) | ((pe32_u32)p[3] << 24);
}

static void write32(pe32_u8 *p, pe32_u32 n)
{
    p[0] = (pe32_u8)n;
    p[1] = (pe32_u8)(n >> 8);
    p[2] = (pe32_u8)(n >> 16);
    p[3] = (pe32_u8)(n >> 24);
}

static const char *cstring(const struct pe32_module *module, pe32_u32 rva)
{
    pe32_u32 pos;
    if (!module->image || rva >= module->size) {
        return (const char *)0;
    }
    for (pos = rva; pos < module->size; ++pos) {
        if (!module->image[pos]) {
            return (const char *)(module->image + rva);
        }
    }
    return (const char *)0;
}

static int equal_names(const char *a, const char *b, int ignore_case)
{
    unsigned char left, right;
    if (!a || !b) {
        return 0;
    }
    for (; *a && *b; ++a, ++b) {
        left = (unsigned char)*a;
        right = (unsigned char)*b;
        if (ignore_case) {
            if (left >= 'A' && left <= 'Z') {
                left += 'a' - 'A';
            }
            if (right >= 'A' && right <= 'Z') {
                right += 'a' - 'A';
            }
        }
        if (left != right) {
            return 0;
        }
    }
    return *a == *b;
}

static const char *basename(const char *path)
{
    const char *last = path;
    if (!path) {
        return (const char *)0;
    }
    for (; *path; ++path) {
        if (*path == '/' || *path == '\\') {
            last = path + 1;
        }
    }
    return last;
}

static const pe32_u8 *export_directory(const struct pe32_module *module)
{
    if (!module->image) {
        return (const pe32_u8 *)0;
    }
    if (!module->export_rva && !module->export_size) {
        return (const pe32_u8 *)0;
    }
    if (module->export_size < 40 ||
        !contains(module->size, module->export_rva, module->export_size)) {
        return (const pe32_u8 *)0;
    }
    return module->image + module->export_rva;
}

const char *pe32_export_module_name(const struct pe32_module *module)
{
    const pe32_u8 *directory = export_directory(module);
    return directory ? cstring(module, read32(directory + 12)) : (const char *)0;
}

enum pe32_link_status pe32_find_export(const struct pe32_module *module,
                                        const char *name, pe32_u32 ordinal,
                                        pe32_u32 *address)
{
    const pe32_u8 *directory = export_directory(module);
    pe32_u32 base_ordinal, functions, names, functions_rva, names_rva;
    pe32_u32 ordinals_rva, index, function_rva, i;

    if (address) {
        *address = 0;
    }
    if (!directory) {
        return (module->export_rva || module->export_size) ?
            PE32_LINK_INVALID : PE32_LINK_MISSING;
    }
    base_ordinal = read32(directory + 16);
    functions = read32(directory + 20);
    names = read32(directory + 24);
    functions_rva = read32(directory + 28);
    names_rva = read32(directory + 32);
    ordinals_rva = read32(directory + 36);
    if (!functions || functions > module->size / 4 ||
        names > module->size / 4 ||
        !contains(module->size, functions_rva, functions * 4) ||
        !contains(module->size, names_rva, names * 4) ||
        !contains(module->size, ordinals_rva, names * 2)) {
        return PE32_LINK_INVALID;
    }
    if (name) {
        index = functions;
        for (i = 0; i < names; ++i) {
            const char *entry_name = cstring(module,
                read32(module->image + names_rva + i * 4));
            if (!entry_name) {
                return PE32_LINK_INVALID;
            }
            if (equal_names(name, entry_name, 0)) {
                index = read16(module->image + ordinals_rva + i * 2);
                break;
            }
        }
        if (index == functions) {
            return PE32_LINK_MISSING;
        }
    } else {
        if (ordinal < base_ordinal || ordinal - base_ordinal >= functions) {
            return PE32_LINK_MISSING;
        }
        index = ordinal - base_ordinal;
    }
    if (index >= functions) {
        return PE32_LINK_INVALID;
    }
    function_rva = read32(module->image + functions_rva + index * 4);
    if (!function_rva) {
        return PE32_LINK_MISSING;
    }
    if (!contains(module->size, function_rva, 1)) {
        return PE32_LINK_INVALID;
    }
    if (function_rva >= module->export_rva &&
        function_rva - module->export_rva < module->export_size) {
        return cstring(module, function_rva) ?
            PE32_LINK_FORWARDER : PE32_LINK_INVALID;
    }
    if (module->base > 0xffffffffu - function_rva) {
        return PE32_LINK_INVALID;
    }
    if (address) {
        *address = module->base + function_rva;
    }
    return PE32_LINK_OK;
}

const struct pe32_module *pe32_find_module(const struct pe32_module *modules,
                                           pe32_u32 count, const char *name)
{
    pe32_u32 i;
    for (i = 0; i < count; ++i) {
        const char *export_name = pe32_export_module_name(&modules[i]);
        if (equal_names(name, export_name, 1) ||
            equal_names(name, modules[i].path, 1) ||
            equal_names(name, basename(modules[i].path), 1)) {
            return &modules[i];
        }
    }
    return (const struct pe32_module *)0;
}

const struct pe32_module *pe32_find_module_base(const struct pe32_module *modules,
                                                pe32_u32 count, pe32_u32 base)
{
    pe32_u32 i;
    for (i = 0; i < count; ++i) {
        if (modules[i].base == base) {
            return &modules[i];
        }
    }
    return (const struct pe32_module *)0;
}

static enum pe32_link_status bind_pass(struct pe32_module *target,
                                       const struct pe32_module *modules,
                                       pe32_u32 module_count,
                                       pe32_link_report report, void *context,
                                       pe32_u32 *linked_count, int apply,
                                       pe32_external_resolver resolver,
                                       void *resolver_context)
{
    pe32_u32 pos, count = 0;
    if (!target->import_rva && !target->import_size) {
        if (linked_count) {
            *linked_count = 0;
        }
        return PE32_LINK_OK;
    }
    if (target->import_size < 20 ||
        !contains(target->size, target->import_rva, target->import_size)) {
        return PE32_LINK_INVALID;
    }
    for (pos = 0; pos <= target->import_size - 20; pos += 20) {
        const pe32_u8 *descriptor = target->image + target->import_rva + pos;
        pe32_u32 original = read32(descriptor);
        pe32_u32 timestamp = read32(descriptor + 4);
        pe32_u32 forwarder = read32(descriptor + 8);
        pe32_u32 name_rva = read32(descriptor + 12);
        pe32_u32 first = read32(descriptor + 16);
        const char *dll;
        const struct pe32_module *source;
        pe32_u32 i, lookup_rva;
        if (!(original || timestamp || forwarder || name_rva || first)) {
            if (linked_count) {
                *linked_count = count;
            }
            return PE32_LINK_OK;
        }
        dll = cstring(target, name_rva);
        if (!dll || !first) {
            return PE32_LINK_INVALID;
        }
        source = pe32_find_module(modules, module_count, dll);
        lookup_rva = original ? original : first;
        for (i = 0; i < target->size / 4; ++i) {
            pe32_u32 lookup, addr, ordinal = 0;
            const char *name = (const char *)0;
            enum pe32_link_status status;
            if (!contains(target->size, lookup_rva, 4) ||
                !contains(target->size, first, 4)) {
                return PE32_LINK_INVALID;
            }
            lookup = read32(target->image + lookup_rva);
            if (!lookup) {
                break;
            }
            if (lookup & 0x80000000u) {
                if (lookup & 0x7fff0000u) {
                    return PE32_LINK_INVALID;
                }
                ordinal = lookup & 0xffff;
            } else {
                if (!contains(target->size, lookup, 2)) {
                    return PE32_LINK_INVALID;
                }
                name = cstring(target, lookup + 2);
                if (!name) {
                    return PE32_LINK_INVALID;
                }
            }
            addr = 0;
            if (source) {
                status = pe32_find_export(source, name, ordinal, &addr);
            } else if (resolver) {
                status = resolver(resolver_context, dll, name, ordinal, &addr);
            } else {
                status = PE32_LINK_MISSING;
            }
            if (!apply && status != PE32_LINK_OK) {
                if (report) report(context, dll, name, ordinal, status, 0);
                return status;
            }
            if (apply) {
                if (status != PE32_LINK_OK) {
                    return status;
                }
                write32(target->image + first, addr);
                if (report) report(context, dll, name, ordinal, status, addr);
            }
            ++count;
            if (lookup_rva > 0xffffffffu - 4 || first > 0xffffffffu - 4) {
                return PE32_LINK_INVALID;
            }
            lookup_rva += 4;
            first += 4;
        }
        if (i == target->size / 4) {
            return PE32_LINK_INVALID; /* Unterminated thunk table. */
        }
    }
    return PE32_LINK_INVALID; /* Missing null import descriptor. */
}

enum pe32_link_status pe32_bind_imports(struct pe32_module *target,
                                        const struct pe32_module *modules,
                                        pe32_u32 module_count,
                                        pe32_link_report report, void *context,
                                        pe32_u32 *linked_count)
{
    return pe32_bind_imports_with_host(target, modules, module_count,
                                       report, context, linked_count,
                                       (pe32_external_resolver)0, (void *)0);
}

enum pe32_link_status pe32_bind_imports_with_host(
    struct pe32_module *target, const struct pe32_module *modules,
    pe32_u32 module_count, pe32_link_report report, void *report_context,
    pe32_u32 *linked_count, pe32_external_resolver resolver,
    void *resolver_context)
{
    enum pe32_link_status status;
    if (linked_count) {
        *linked_count = 0;
    }
    status = bind_pass(target, modules, module_count, report, report_context,
                       0, 0, resolver, resolver_context);
    if (status != PE32_LINK_OK) {
        return status;
    }
    return bind_pass(target, modules, module_count, report, report_context,
                     linked_count, 1, resolver, resolver_context);
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_res.h"

enum {
    ERROR_RESOURCE_DATA_NOT_FOUND = 1812,
    ERROR_RESOURCE_TYPE_NOT_FOUND = 1813,
    ERROR_RESOURCE_NAME_NOT_FOUND = 1814,
    ERROR_INVALID_PARAMETER = 87,
    DIRECTORY_ENTRY_RESOURCE = 2
};

static pe32_u32 read_u16(const pe32_u8 *p) { return p[0] | (pe32_u32)p[1] << 8; }
static pe32_u32 read_u32(const pe32_u8 *p)
{
    return read_u16(p) | read_u16(p + 2) << 16;
}

static int in_range(const struct pe32_module *m, pe32_u32 off, pe32_u32 len)
{
    return off <= m->size && len <= m->size - off;
}

static pe32_u32 ascii_upper(pe32_u32 c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* Resource directory entry ids are numbers; names are length-prefixed UTF-16
 * strings relative to the resource section start. */
static int name_matches(const struct pe32_module *m, pe32_u32 root,
                        pe32_u32 name_field, const char *name)
{
    pe32_u32 off = root + (name_field & 0x7fffffffu), len, i;
    if (!in_range(m, off, 2)) {
        return 0;
    }
    len = read_u16(m->image + off);
    if (!in_range(m, off + 2, len * 2)) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!name[i] ||
            ascii_upper(read_u16(m->image + off + 2 + i * 2)) !=
                ascii_upper((pe32_u8)name[i])) {
            return 0;
        }
    }
    return name[len] == 0;
}

/* Returns the offset of the entry's target (high bit = subdirectory), or
 * 0xffffffff when no entry matches. A NULL name selects the first entry. */
static pe32_u32 find_entry(const struct pe32_module *m, pe32_u32 root,
                           pe32_u32 dir, pe32_u32 id, const char *name,
                           int any)
{
    pe32_u32 named, ids, i, entry;
    if (!in_range(m, root + dir, 16)) {
        return 0xffffffffu;
    }
    named = read_u16(m->image + root + dir + 12);
    ids = read_u16(m->image + root + dir + 14);
    if (!in_range(m, root + dir + 16, (named + ids) * 8)) {
        return 0xffffffffu;
    }
    for (i = 0; i < named + ids; i++) {
        entry = root + dir + 16 + i * 8;
        if (any) {
            return read_u32(m->image + entry + 4);
        }
        if (name ? (i < named && name_matches(m, root,
                                              read_u32(m->image + entry), name))
                 : (i >= named && read_u32(m->image + entry) == id)) {
            return read_u32(m->image + entry + 4);
        }
    }
    return 0xffffffffu;
}

/* Parses "#123" decimal strings; returns -1 for ordinary names. */
static long parse_numeric(const char *s)
{
    long v = 0;
    if (s[0] != '#' || !s[1]) {
        return -1;
    }
    for (s++; *s; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10 + (*s - '0');
        if (v > 0xffff) {
            return -1;
        }
    }
    return v;
}

static pe32_u32 descend(const struct pe32_module *m, pe32_u32 root,
                        pe32_u32 dir, const char *key, pe32_u32 missing)
{
    pe32_u32 target;
    if ((unsigned long)key <= 0xfffful) {
        target = find_entry(m, root, dir, (pe32_u32)(unsigned long)key, 0, 0);
    } else if (parse_numeric(key) >= 0) {
        target = find_entry(m, root, dir, (pe32_u32)parse_numeric(key), 0, 0);
    } else {
        target = find_entry(m, root, dir, 0, key, 0);
    }
    if (target == 0xffffffffu || !(target & 0x80000000u)) {
        pe32_win32_set_last_error(missing);
        return 0xffffffffu;
    }
    return target & 0x7fffffffu;
}

pe32_u32 PE32_WINAPI pe32_win32_find_resource_a(pe32_u32 module,
                                                const char *name,
                                                const char *type)
{
    const struct pe32_module *m = pe32_win32_module_for_handle(module);
    pe32_u32 dirs, root, rva, size, dir, leaf;
    if (!m || !name || !type || m->size < 0x40) {
        pe32_win32_set_last_error(m ? ERROR_INVALID_PARAMETER : 6);
        return 0;
    }
    /* e_lfanew -> PE header; optional header data directories at +0x78. */
    dirs = read_u32(m->image + 0x3c);
    if (!in_range(m, dirs, 0x88 + 8 * 3)) {
        pe32_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
    rva = read_u32(m->image + dirs + 0x78 + 8 * DIRECTORY_ENTRY_RESOURCE);
    size = read_u32(m->image + dirs + 0x7c + 8 * DIRECTORY_ENTRY_RESOURCE);
    if (!rva || !size || !in_range(m, rva, size)) {
        pe32_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
    root = rva;
    dir = descend(m, root, 0, type, ERROR_RESOURCE_TYPE_NOT_FOUND);
    if (dir == 0xffffffffu) {
        return 0;
    }
    dir = descend(m, root, dir, name, ERROR_RESOURCE_NAME_NOT_FOUND);
    if (dir == 0xffffffffu) {
        return 0;
    }
    leaf = find_entry(m, root, dir, 0, 0, 1);
    if (leaf == 0xffffffffu || (leaf & 0x80000000u) ||
        !in_range(m, root + leaf, 16)) {
        pe32_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
    return m->base + root + leaf;
}

static const pe32_u8 *entry_for(const struct pe32_module *m, pe32_u32 res)
{
    if (!m || res < m->base || !in_range(m, res - m->base, 16)) {
        return 0;
    }
    return m->image + (res - m->base);
}

pe32_u32 PE32_WINAPI pe32_win32_load_resource(pe32_u32 module, pe32_u32 res)
{
    const struct pe32_module *m = pe32_win32_module_for_handle(module);
    const pe32_u8 *entry = entry_for(m, res);
    pe32_u32 rva;
    if (!entry) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    rva = read_u32(entry);
    if (!in_range(m, rva, read_u32(entry + 4))) {
        pe32_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
    return m->base + rva;
}

pe32_u32 PE32_WINAPI pe32_win32_lock_resource(pe32_u32 data) { return data; }

pe32_u32 PE32_WINAPI pe32_win32_sizeof_resource(pe32_u32 module, pe32_u32 res)
{
    const pe32_u8 *entry = entry_for(pe32_win32_module_for_handle(module), res);
    if (!entry) {
        pe32_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return read_u32(entry + 4);
}

pe32_u32 PE32_WINAPI pe32_win32_free_resource(pe32_u32 data)
{
    (void)data;
    return 0; /* Resources live in the image; nothing to free. */
}

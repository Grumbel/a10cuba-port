/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* One 16-byte stdcall trampoline per pe32_stubs.def entry: report the stub
 * index to pe32_stub_dispatch (cdecl), return its value and pop the guest's
 * arguments. Index must match the table order in pe32_stubs.c. */
    .text
    .globl pe32_stub_base
    .balign 16
pe32_stub_base:
    .set pe32_stub_index, 0
#define X(dll, name, args, result) \
    .balign 16; \
    pushl $pe32_stub_index; \
    call pe32_stub_dispatch; \
    addl $4, %esp; \
    .if args; ret $(args * 4); .else; ret; .endif; \
    .set pe32_stub_index, pe32_stub_index + 1
#include "pe32_stubs.def"
#undef X
    .section .note.GNU-stack,"",@progbits

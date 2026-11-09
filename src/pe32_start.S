/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* A tiny i386 Linux entry point: no 32-bit libc or dynamic loader required. */
    .section .text
    .globl _start
    .type _start, @function
_start:
    xorl %ebp, %ebp
    popl %eax                   /* argc */
    movl %esp, %edx             /* argv */
    pushl %edx
    pushl %eax
    call pe32_main
    movl %eax, %ebx
    movl $1, %eax               /* Linux i386 exit */
    int $0x80
    hlt
    .size _start, .-_start
    .section .note.GNU-stack,"",@progbits

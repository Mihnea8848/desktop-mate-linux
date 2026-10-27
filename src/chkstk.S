/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: MIT
 *
 * ___chkstk_ms stack probe, normally from libgcc: touches every 4 KiB page
 * between rsp and rsp - rax.
 */
.text
.globl ___chkstk_ms
___chkstk_ms:
    pushq %rcx
    pushq %rax
    cmpq  $0x1000, %rax
    leaq  24(%rsp), %rcx
    jb    2f
1:  subq  $0x1000, %rcx
    orl   $0, (%rcx)
    subq  $0x1000, %rax
    cmpq  $0x1000, %rax
    ja    1b
2:  subq  %rax, %rcx
    orl   $0, (%rcx)
    popq  %rax
    popq  %rcx
    ret

.section .text.boot
.global _start

_start:
la sp,stack_top
li t0,65536
add sp,sp,t0
tail sbi_main



.section .data
.align 12
.global stack_bottom
//开一个64KB的栈区
stack_top:
    .skip 4096*16

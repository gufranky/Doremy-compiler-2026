    .text
    .globl _start
_start:
    call main
    mv s0, a0
    call __sysy_flush_output
    mv a0, s0
    li a7, 93
    ecall
1:
    j 1b

.section .text
.intel_syntax noprefix
.global _start

_start:
    xor rbp, rbp
    mov rdi, 0
    mov rsi, 0
    call main

    mov rdi, rax
    mov rax, 60
    syscall

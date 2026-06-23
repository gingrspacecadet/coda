.section .text
.intel_syntax noprefix
.global _C3std6system7syscall

_C3std6system7syscall:
    mov rax, rdi
    mov r10, rcx
    syscall
    ret

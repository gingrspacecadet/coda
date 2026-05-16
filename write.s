.intel_syntax noprefix
.global write

write:
    mov rax, 1  /* WRITE syscall */
    /* because of calling conventions, all the args are already in the right registers! */
    syscall

.global _start
.extern main
_start:
    call main
    mov rdi, rax
    mov rax, 60
    syscall

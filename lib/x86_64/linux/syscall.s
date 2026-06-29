.intel_syntax noprefix
.global _C3std6system7syscall   # mangled name for std::system::syscall
.type _C3std6system7syscall, @function

_C3std6system7syscall:
    push rbp
    mov rbp, rsp

    mov rax, rdi        

    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9

    mov r9, QWORD PTR [rbp+16] 

    syscall

    mov rsp, rbp
    pop rbp
    ret

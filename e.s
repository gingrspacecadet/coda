.intel_syntax noprefix
.global main
main:
    push rbp
    mov rbp, rsp
.L0:
    call test
    mov QWORD PTR [rbp - 8], rax
    mov rax, QWORD PTR [rbp - 8]
    mov rsp, rbp
    pop rbp
    ret

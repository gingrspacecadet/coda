.intel_syntax noprefix
.global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
.L0:
    lea rax, QWORD PTR [rip + .Lstr0]
    mov QWORD PTR [rbp - 8], rax
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret

.section .rodata
.Lstr0:
    .string "hello, world!"

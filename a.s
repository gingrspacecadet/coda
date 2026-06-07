.intel_syntax noprefix
.text

.global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
.Lblock_0:
    mov rax, 3
    mov QWORD PTR [rbp-16], rax
    mov rax, 4
    mov QWORD PTR [rbp-8], rax
    mov rax, QWORD PTR [rbp-8]
    mov rsp, rbp
    pop rbp
    ret


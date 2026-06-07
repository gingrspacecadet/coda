.intel_syntax noprefix
.text

.global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
.Lblock_0:
    mov rax, 42
    mov QWORD PTR [rbp-8], rax
    lea rax, [rbp-8]
    mov QWORD PTR [rbp-16], rax
    mov rax, QWORD PTR [rbp-16]
    mov QWORD PTR [rbp-24], rax
    mov rax, 20
    mov rbx, QWORD PTR [rbp-24]
    mov QWORD PTR [rbx], rax
    mov rax, QWORD PTR [rbp-8]
    mov rsp, rbp
    pop rbp
    ret


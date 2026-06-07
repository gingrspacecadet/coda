.intel_syntax noprefix
.text

.global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
.Lblock_0:
    mov rax, 140124368138282
    mov QWORD PTR [rbp-8], rax
    mov rax, QWORD PTR [rbp]
    mov rsp, rbp
    pop rbp
    ret


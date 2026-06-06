call $main
halt
test:
    push rfp
    mov rfp, rsp
.L0:
    mov r0, #0
    pop rfp
    ret
main:
    push rfp
    mov rfp, rsp
    mov [rfp - #4], r0
.L1:
    mov r0, [rfp - #4]
    mov [rfp - #4], r0
    mov r0, [rfp - #4]
    cmp r0, #2
    setne al
    movzx r0, al
    mov [rfp - #8], r0
    mov r0, [rfp - #8]
    cmp r0, #0
    je .L3
.L2:
    jmp .L4
.L3:
.L4:
    mov [rfp - #8], #0
.L5:
    mov r0, [rfp - #4]
    mov [rfp - #12], r0
    mov r0, [rfp - #8]
    cmp r0, [rfp - #12]
    setl al
    movzx r0, al
    mov [rfp - #16], r0
    mov r0, [rfp - #16]
    cmp r0, #0
    je .L7
.L6:
    mov r0, [rfp - #8]
    mov [rfp - #20], r0
    mov r0, [rfp - #20]
    add r0, r0, #1
    mov [rfp - #20], r0
    mov r0, [rfp - #20]
    mov [rfp - #8], r0
    jmp .L5
.L7:
    mov [rfp - #12], #1
    mov [rfp - #16], #42424242
    mov r0, [rfp - #16]
    mov [rfp - #20], r0
    pop rfp
    ret

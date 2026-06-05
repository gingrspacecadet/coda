call $main
halt
print:
    push rfp
    mov rfp, rsp
    mov [rfp - #4], r0
.L0:
    mov r0, [rfp - #4]
    mov [rfp - #4], r0
    mov r0, [rfp - #4]
    mov [rfp - #8], r0
    mov r0, [rfp - #4]
    mov r1, [rfp - #8]
    call write
    mov [rfp - #12], r12
    pop rfp
    ret
main:
    push rfp
    mov rfp, rsp
.L1:
    lea r0, [rip + .LC0]
    call print
    mov [rfp - #4], r12
    mov r0, #0
    pop rfp
    ret

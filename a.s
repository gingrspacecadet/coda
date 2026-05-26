call $main
halt
main:
    push rfp
    mov rfp, rsp
.L0:
    mov [rfp - #4], #42
    mov r0, [rfp - #4]
    pop rfp
    ret

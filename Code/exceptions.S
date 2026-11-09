.text
.extern exception_report
.macro ISR vector
.globl exception_\vector
exception_\vector:
    .if (\vector != 8) && (\vector != 10) && (\vector != 11) && (\vector != 12) && (\vector != 13) && (\vector != 14) && (\vector != 17) && (\vector != 21) && (\vector != 29) && (\vector != 30)
        pushq $0
    .endif
    pushq $\vector
    jmp exception_common
.endm
.irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ISR \vector
.endr

.globl unexpected_interrupt
unexpected_interrupt:
    pushq $0
    pushq $255
exception_common:
    cli
    cld
    movq %rsp, %rcx
    // Fatal handlers do not return. Use a separate stack for the C report.
    leaq exception_stack_end(%rip), %rsp
    andq $-16, %rsp
    subq $32, %rsp
    call exception_report
1:  hlt
    jmp 1b

.section .rdata,"dr"
.globl exception_entries
exception_entries:
.irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad exception_\vector
.endr
.bss
.balign 16
exception_stack:
    .skip 16384
exception_stack_end:

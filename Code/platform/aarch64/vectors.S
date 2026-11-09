.text
.balign 2048
.globl platform_vectors
platform_vectors:
.rept 16
    b platform_exception_entry
    .balign 128
.endr
platform_exception_entry:
    msr daifset, #15
    mrs x2, CurrentEL
    cmp x2, #8
    b.eq 1f
    mrs x0, esr_el1
    mrs x1, elr_el1
    b platform_exception_report
1:
    mrs x0, esr_el2
    mrs x1, elr_el2
    b platform_exception_report

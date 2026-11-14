.syntax unified
.cpu cortex-m4
.thumb
.section .text.jump_to_application,"ax",%progbits
.global jump_to_application
.thumb_func
jump_to_application:
    dsb
    isb
    msr msp, r0
    cpsie i
    bx r1

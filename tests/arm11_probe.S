// SPDX-License-Identifier: GPL-3.0-or-later
// Original guest program. Pass marker requires integer flags, memory, ARM/Thumb
// interworking and VFP arithmetic to agree with the known expected results.
.syntax unified
.arch armv6k
.fpu vfp
.arm
.text
.global _start
_start:
    mov r4, #42
    sub sp, sp, #8
    str r4, [sp]
    ldr r5, [sp]
    add sp, sp, #8
    cmp r5, #42
    bne .Lfail
    mvn r2, #0
    adds r2, r2, #1
    bcc .Lfail
    bne .Lfail
    mov r2, #6
    mov r3, #7
    mul r4, r2, r3
    cmp r4, #42
    bne .Lfail
    adr r3, .Lthumb
    orr r3, r3, #1
    blx r3
    cmp r0, #7
    bne .Lfail
    vldr s0, .Lone
    vldr s1, .Ltwo
    vadd.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, .Lthree
    cmp r0, r1
    bne .Lfail
    adr r0, .Lpass
    mov r1, #(.Lpass_end - .Lpass)
    svc #0x3d
    b .Lsleep
.Lfail:
    adr r0, .Lfailure
    mov r1, #(.Lfailure_end - .Lfailure)
    svc #0x3d
.Lsleep:
    mov r0, #0
    mov r1, #0
    svc #0x0a
    b .Lsleep
.align 2
.Lone: .float 1.0
.Ltwo: .float 2.0
.Lthree: .float 3.0
.Lpass: .ascii "CTR_RUNTIME_ARM11_PROBE_OK"
.Lpass_end:
.Lfailure: .ascii "CTR_RUNTIME_PROBE_FAILED"
.Lfailure_end:
.align 2
.thumb
.thumb_func
.Lthumb:
    movs r0, #3
    adds r0, #4
    bx lr

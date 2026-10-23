/*
    Copyright 2016-2025 melonDS team, RSDuck

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "../ARMJIT_x64/ARMJIT_Offsets.h"

.text

#define RCPSR w27
#define RCycles w28
#define RCPU x29

.p2align 4,,15

#ifdef __APPLE__
.global _ARM_Dispatch
_ARM_Dispatch:
#else
.global ARM_Dispatch
ARM_Dispatch:
#endif
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]

    mov RCPU, x0
    ldr RCycles, [RCPU, ARM_Cycles_offset]
    ldr RCPSR, [RCPU, ARM_CPSR_offset]

    br x1

.p2align 4,,15

#ifdef __APPLE__
.global _ARM_Ret
_ARM_Ret:
#else
.global ARM_Ret
ARM_Ret:
#endif
    str RCycles, [RCPU, ARM_Cycles_offset]
    str RCPSR, [RCPU, ARM_CPSR_offset]

    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96

    ret

.p2align 4,,15

.global ARM_RestoreContext
ARM_RestoreContext:
    mov sp, x0

    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]

    ldp x17, x18, [sp, #248]
    mov sp, x17

    br x18

#ifdef __SWITCH__
// ssrecomp: back to the JIT code that faulted, from the Switch's exception handler
// (ARMJIT_Memory.cpp). x0: libnx's ThreadExceptionDump, x1: where to go on. Every register is
// put back as it was, the NEON registers and the flags too, but for x17, which carries the
// address of the jump at the end (no JIT code keeps a value in x17).
.global ARM_RestoreExceptionDump
ARM_RestoreExceptionDump:
    str x1, [x0, #272]

    add x2, x0, #288
    ldp q0, q1, [x2, #0]
    ldp q2, q3, [x2, #32]
    ldp q4, q5, [x2, #64]
    ldp q6, q7, [x2, #96]
    ldp q8, q9, [x2, #128]
    ldp q10, q11, [x2, #160]
    ldp q12, q13, [x2, #192]
    ldp q14, q15, [x2, #224]
    ldp q16, q17, [x2, #256]
    ldp q18, q19, [x2, #288]
    ldp q20, q21, [x2, #320]
    ldp q22, q23, [x2, #352]
    ldp q24, q25, [x2, #384]
    ldp q26, q27, [x2, #416]
    ldp q28, q29, [x2, #448]
    ldp q30, q31, [x2, #480]

    ldr w2, [x0, #800]
    and w2, w2, #0xF0000000
    msr nzcv, x2

    mov x16, x0
    ldp x0, x1, [x16, #16]
    ldp x2, x3, [x16, #32]
    ldp x4, x5, [x16, #48]
    ldp x6, x7, [x16, #64]
    ldp x8, x9, [x16, #80]
    ldp x10, x11, [x16, #96]
    ldp x12, x13, [x16, #112]
    ldp x14, x15, [x16, #128]
    ldp x18, x19, [x16, #160]
    ldp x20, x21, [x16, #176]
    ldp x22, x23, [x16, #192]
    ldp x24, x25, [x16, #208]
    ldp x26, x27, [x16, #224]
    ldr x28, [x16, #240]
    ldp x29, x30, [x16, #248]
    ldr x17, [x16, #264]
    mov sp, x17
    ldr x17, [x16, #272]
    ldr x16, [x16, #144]
    br x17
#endif

#if !defined(__APPLE__) && !defined(__WIN32__)
.section .note.GNU-stack,"",@progbits
#endif


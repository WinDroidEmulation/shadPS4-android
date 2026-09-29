// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/types.h"

#if defined(__x86_64__)
asm(".att_syntax prefix");
#endif

#if defined(__x86_64__)
asm(R"(
.global _sceFiberSetJmp
_sceFiberSetJmp:
    movq %rax, 0x0(%rdi)

    movq (%rsp), %rdx
    movq %rdx, 0x10(%rdi)

    movq %rcx, 0x08(%rdi)
    movq %rbx, 0x18(%rdi)
    movq %rsp, 0x20(%rdi)
    movq %rbp, 0x28(%rdi)

    movq %r8,  0x30(%rdi)
    movq %r9,  0x38(%rdi)
    movq %r10, 0x40(%rdi)
    movq %r11, 0x48(%rdi)
    movq %r12, 0x50(%rdi)
    movq %r13, 0x58(%rdi)
    movq %r14, 0x60(%rdi)
    movq %r15, 0x68(%rdi)

    fnstcw  0x70(%rdi)
    stmxcsr 0x72(%rdi)

    xor %eax, %eax
    ret

.global _sceFiberLongJmp
_sceFiberLongJmp:
    # MXCSR = (MXCSR & 0x3f) ^ (ctx->mxcsr & ~0x3f)
    stmxcsr -0x4(%rsp)
    movl 0x72(%rdi), %eax
    andl $0xffffffc0, %eax
    movl -0x4(%rsp), %ecx
    andl $0x3f, %ecx
    xorl %eax, %ecx
    movl %ecx, -0x4(%rsp)
    ldmxcsr -0x4(%rsp)

    movq 0x00(%rdi), %rax
    movq 0x08(%rdi), %rcx
    movq 0x10(%rdi), %rdx
    movq 0x18(%rdi), %rbx
    movq 0x20(%rdi), %rsp
    movq 0x28(%rdi), %rbp

    movq 0x30(%rdi), %r8
    movq 0x38(%rdi), %r9
    movq 0x40(%rdi), %r10
    movq 0x48(%rdi), %r11
    movq 0x50(%rdi), %r12
    movq 0x58(%rdi), %r13
    movq 0x60(%rdi), %r14
    movq 0x68(%rdi), %r15

    fldcw 0x70(%rdi)

    # Make the jump and return 1
    movq %rdx, 0x00(%rsp)
    movl $0x1, %eax
    ret

.global _sceFiberSwitchEntry
_sceFiberSwitchEntry:
    mov %rdi, %r11

    # Set stack address to provided stack
    movq 0x18(%r11), %rsp
    xorl %ebp, %ebp

    movq 0x20(%r11), %r10 # data->state

    # Set previous fiber state to Idle
    test %r10, %r10
    jz .clear_regs
    movl $2, (%r10)

.clear_regs:
    test %esi, %esi
    jz .skip_fpu_regs

    ldmxcsr 0x2c(%r11)
    fldcw 0x28(%r11)

.skip_fpu_regs:
    movq 0x08(%r11), %rdi # data->arg_on_initialize
    movq 0x10(%r11), %rsi # data->arg_on_run_to
    movq 0x00(%r11), %r11 # data->entry

    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    pxor %mm0, %mm0
    pxor %mm1, %mm1
    pxor %mm2, %mm2
    pxor %mm3, %mm3
    pxor %mm4, %mm4
    pxor %mm5, %mm5
    pxor %mm6, %mm6
    pxor %mm7, %mm7
    emms
    vzeroall

    # Call the fiber's entry function: entry(arg_on_initialize, arg_on_run_to)
    call *%r11

    # Fiber returned, not good
    movl $1, %edi
    call _sceFiberForceQuit
    ret
)");
#elif defined(__aarch64__)
// AArch64 fiber context switching.
//
// Register save area layout inside OrbisFiberContext (see fiber.h):
//   0x00 x19        0x08 x20        0x10 x21        0x18 x22
//   0x20 sp (rsp)   0x28 x29 (rbp)  0x30 x30 (lr)   0x38 x23
//   0x40 x24        0x48 x25        0x50 x26        0x58 x27
//   0x60 x28        0x68 d8         0x70 fpcr       0x74 fpsr
//   0x78 d9 .. 0xA8 d15 (fp_regs extension in OrbisFiberContext)
// The ctx.rsp / ctx.rbp fields keep the same meaning they have on x86_64.
asm(R"(
.global _sceFiberSetJmp
_sceFiberSetJmp:
    stp x19, x20, [x0]
    stp x21, x22, [x0, #16]
    mov x10, sp
    str x10, [x0, #32]
    stp x29, x30, [x0, #40]
    stp x23, x24, [x0, #56]
    stp x25, x26, [x0, #72]
    stp x27, x28, [x0, #88]
    str d8, [x0, #104]
    mrs x10, fpcr
    str w10, [x0, #112]
    mrs x10, fpsr
    str w10, [x0, #116]
    stp d9, d10, [x0, #120]
    stp d11, d12, [x0, #136]
    stp d13, d14, [x0, #152]
    str d15, [x0, #168]
    mov w0, wzr
    ret

.global _sceFiberLongJmp
_sceFiberLongJmp:
    ldp x19, x20, [x0]
    ldp x21, x22, [x0, #16]
    ldr x10, [x0, #32]
    mov sp, x10
    ldp x29, x30, [x0, #40]
    ldp x23, x24, [x0, #56]
    ldp x25, x26, [x0, #72]
    ldp x27, x28, [x0, #88]
    ldr d8, [x0, #104]
    ldr w10, [x0, #112]
    msr fpcr, x10
    ldr w10, [x0, #116]
    msr fpsr, x10
    ldp d9, d10, [x0, #120]
    ldp d11, d12, [x0, #136]
    ldp d13, d14, [x0, #152]
    ldr d15, [x0, #168]
    mov w0, #1
    ret

.global _sceFiberSwitchEntry
_sceFiberSwitchEntry:
    // x0 = OrbisFiberData*, w1 = set_fpu
    mov x11, x0
    // Switch to the fiber stack (data->stack_addr at 0x18)
    ldr x16, [x11, #24]
    mov sp, x16
    // Terminate the frame-pointer chain
    mov x29, xzr
    // Mark the previous fiber state Idle (data->state at 0x20)
    ldr x10, [x11, #32]
    cbz x10, 1f
    mov w9, #2
    str w9, [x10]
1:
    // Start the fiber with a clean FP control state.
    msr fpcr, xzr
    msr fpsr, xzr
    // Call the fiber's entry function: entry(arg_on_initialize, arg_on_run_to)
    ldr x0, [x11, #8]
    ldr x1, [x11, #16]
    ldr x9, [x11]
    blr x9
    // Fiber returned, not good
    mov w0, #1
    bl _sceFiberForceQuit
    ret
)");
#endif

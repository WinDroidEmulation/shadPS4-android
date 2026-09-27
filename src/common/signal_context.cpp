// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/arch.h"
#include "common/signal_context.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__FreeBSD__)
#include <machine/npx.h>
#include <sys/ucontext.h>
#else
#include <sys/ucontext.h>
#endif

namespace Common {

#if defined(ARCH_ARM64) && !defined(__APPLE__)
// Android/Linux ARM64: neither bionic nor glibc expose ESR in mcontext_t
// directly. The kernel appends a tagged ESR extension record (ESR_MAGIC,
// 0x45535201) to the reserved area of the signal frame instead, so walk the
// record list to recover it. Falls back to 0 when the record is absent.
static u64 GetArm64Esr(ucontext_t* ctx) {
    const u8* base = reinterpret_cast<const u8*>(ctx->uc_mcontext.__reserved);
    const u8* end = base + sizeof(ctx->uc_mcontext.__reserved);
    for (const u8* p = base; p + sizeof(u32) * 2 <= end;) {
        const u32 magic = *reinterpret_cast<const u32*>(p);
        const u32 size = *reinterpret_cast<const u32*>(p + sizeof(u32));
        if (size == 0) {
            break;
        }
        if (magic == 0x45535201u /* ESR_MAGIC */ &&
            p + sizeof(u32) * 2 + sizeof(u64) <= end) {
            return *reinterpret_cast<const u64*>(p + sizeof(u32) * 2);
        }
        p += size;
    }
    return 0;
}
#endif

void* GetRip(void* ctx) {
#if defined(_WIN32)
    return (void*)((EXCEPTION_POINTERS*)ctx)->ContextRecord->Rip;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(ARCH_ARM64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__pc;
#elif defined(__FreeBSD__)
    return (void*)((ucontext_t*)ctx)->uc_mcontext.mc_rip;
#elif defined(ARCH_ARM64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext.pc;
#elif defined(ARCH_X86_64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP];
#else
#error "Unsupported architecture"
#endif
}

bool IsWriteError(void* ctx) {
#if defined(_WIN32)
    return ((EXCEPTION_POINTERS*)ctx)->ExceptionRecord->ExceptionInformation[0] == 1;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__err & 0x2;
#elif defined(__APPLE__) && defined(ARCH_ARM64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__esr & 0x40;
#elif defined(__FreeBSD__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.mc_err & 0x2;
#elif defined(ARCH_ARM64)
    // ESR_ELx.WnR (bit 6): the aborting access was a write.
    return GetArm64Esr((ucontext_t*)ctx) & 0x40;
#elif defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR] & 0x2;
#else
#error "Unsupported architecture"
#endif
}

bool IsExecuteError(void* ctx) {
#if defined(_WIN32)
    return ((EXCEPTION_POINTERS*)ctx)->ExceptionRecord->ExceptionInformation[0] == 0xf;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__err & 0x10;
#elif defined(__FreeBSD__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.mc_err & 0x10;
#elif defined(ARCH_ARM64)
    // Instruction abort from lower EL (EC 0x20) or current EL (EC 0x21).
    const u64 ec = (GetArm64Esr((ucontext_t*)ctx) >> 26) & 0x3F;
    return ec == 0x20 || ec == 0x21;
#elif defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR] & 0x10;
#else
#error "Unsupported architecture"
#endif
}

} // namespace Common

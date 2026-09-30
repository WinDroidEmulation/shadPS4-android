// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/cpu/native/native_x64_backend.h"

#include "common/assert.h"
#include "common/logging/log.h"

namespace Core::Cpu {

u64 NativeX64Backend::Execute(u64 rip, const GuestCallContext& ctx) {
#if defined(ARCH_X86_64)
    // Direct execution: call the entry point as a function with the
    // PS4 SYSV ABI (args=arg0, argp=arg1, param=arg2). On x86-64 the
    // guest and host ABIs match.
    using EntryFunc = s32 (*)(u64, const void*, void*);
    const EntryFunc entry = reinterpret_cast<EntryFunc>(rip);
    LOG_INFO(Cpu, "NativeX64Backend: jumping to guest entry 0x{:016x}", rip);
    const s32 result = entry(ctx.args, ctx.argp, ctx.param);
    LOG_INFO(Cpu, "NativeX64Backend: guest entry returned {}", result);
    // Return 0 as the "next RIP" — the entry already ran to completion.
    return 0;
#else
    // Should never be reached — ResolveAutoBackend picks X64Interpreter
    // on non-x86 hosts.
    UNREACHABLE_MSG("NativeX64Backend is not available on this architecture");
    return rip;
#endif
}

} // namespace Core::Cpu

// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// CpuBackend — abstract base class shared by all translation backends.
// The runtime picks an implementation via ResolveAutoBackend():
//   - On x86-64 host: NativeX64Backend (jump straight to the guest code).
//   - On ARM64 host: X64InterpreterBackend initially; Aarch64JitBackend
//                    once JIT is wired up.
//
// Subclasses implement Execute(rip, ctx) which runs guest code starting at
// `rip` with the register state in `ctx`, returning the new RIP when the
// block exits (a non-local jump, a HLE call, a page fault we couldn't
// service, or the user pausing the emulator).

#pragma once

#include <string>
#include <variant>

#include "src/core/cpu/x64_cpu_state.h"

namespace Core::Cpu {

// Which backend to use. Resolved at runtime via SetBackendKind() or
// ResolveAutoBackend().
enum class CpuBackendKind : u8 {
    // Auto-pick based on host architecture. The default; mirrors what the
    // upstream x86-64 shadPS4 binary does on x86 hosts and what the
    // Android ARM64 APK's libshadps4.so does on ARM hosts.
    Auto = 0,
    // Direct native execution on x86-64 hosts. Selected by Auto when
    // ARCH_X86_64 is defined. On ARM64 the runtime never selects this.
    NativeX64 = 1,
    // Zydis-driven instruction-by-instruction interpreter. Selected by
    // Auto on ARM64 until the JIT is wired up.
    X64Interpreter = 2,
    // ARM64 JIT — translates x86-64 basic blocks into ARM64 code at runtime.
    // Selected by Auto on ARM64 once available.
    Aarch64Jit = 3,
};

// Convert a backend kind to a human-readable name for diagnostics.
const char* CpuBackendKindToString(CpuBackendKind kind);

// Carries the arguments the guest entry point expects: argc/argv/envp on
// the PS4 these come from the module's OrbisProcParam.  Right now we only
// need the entrypoint-args form; richer state can be added later.
struct GuestCallContext {
    u64 args = 0;
    const void* argp = nullptr;
    void* param = nullptr;
};

// Subclass error type, thrown from Execute() on a hard error we couldn't
// recover from. Caught at the emulator main-loop level.
struct CpuBackendError {
    std::string what;
};

class CpuBackend {
public:
    virtual ~CpuBackend() = default;

    // Run guest code starting at `rip` with the register state in `ctx`.
    // Returns the new RIP after the block exits.
    virtual u64 Execute(u64 rip, const GuestCallContext& ctx) = 0;

    // Returns the backend kind (used for logging / settings UI).
    virtual CpuBackendKind Kind() const = 0;

    // Optional hook for backends that need a runtime configuration update
    // mid-flight (e.g. the JIT's compile-on-demand vs AOT mode).  Default
    // impl is a no-op; backends override if they care.
    virtual void SetRuntimeConfig(const std::string& /*key*/, const std::string& /*value*/) {}
};

// Backend selection / lifecycle (defined in cpu_backend_manager.cpp).
CpuBackend* GetBackend();
CpuBackendKind GetSelectedBackendKind();
CpuBackendKind ResolveAutoBackend();
void SetBackendKind(CpuBackendKind kind);
void ConfigureFromSettings();

} // namespace Core::Cpu

// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/cpu/cpu_backend.h"

#include <cstdlib>
#include <memory>

#include "common/arch.h"
#include "common/logging/log.h"
#include "core/cpu/interpreter/x64_interpreter_backend.h"
#include "core/emulator_settings.h"

#if defined(ARCH_X86_64)
#include "core/cpu/native/native_x64_backend.h"
#endif

#if defined(ARCH_ARM64)
#include "core/cpu/jit/aarch64_jit_backend.h"
#endif

namespace Core::Cpu {

namespace {
std::unique_ptr<CpuBackend> g_backend;
CpuBackendKind g_selected_kind = CpuBackendKind::Auto;
} // namespace

const char* CpuBackendKindToString(CpuBackendKind kind) {
    switch (kind) {
    case CpuBackendKind::Auto:           return "Auto";
    case CpuBackendKind::NativeX64:     return "NativeX64";
    case CpuBackendKind::X64Interpreter:return "X64Interpreter";
    case CpuBackendKind::Aarch64Jit:    return "Aarch64Jit";
    }
    return "Unknown";
}

CpuBackendKind ResolveAutoBackend() {
#if defined(ARCH_X86_64)
    // x86-64 host: jump directly to guest code.
    return CpuBackendKind::NativeX64;
#elif defined(ARCH_ARM64)
    // ARM64 host: use the JIT backend. It translates x86-64 basic
    // blocks to ARM64 machine code at runtime, falling back to the
    // interpreter for untranslated instructions. This gives a 5-20x
    // speedup over pure interpretation for hot code paths.
    return CpuBackendKind::Aarch64Jit;
#else
    return CpuBackendKind::X64Interpreter;
#endif
}

CpuBackendKind GetSelectedBackendKind() {
    return g_selected_kind;
}

void SetBackendKind(CpuBackendKind kind) {
    if (kind == g_selected_kind && g_backend) {
        return;
    }
    if (kind == CpuBackendKind::Auto) {
        kind = ResolveAutoBackend();
    }
    LOG_INFO(Core_Cpu, "Switching CPU backend: {} -> {}", CpuBackendKindToString(g_selected_kind),
             CpuBackendKindToString(kind));
    g_selected_kind = kind;
    switch (kind) {
#if defined(ARCH_X86_64)
    case CpuBackendKind::NativeX64:
        g_backend = std::make_unique<NativeX64Backend>();
        break;
#endif
    case CpuBackendKind::X64Interpreter:
        g_backend = std::make_unique<X64InterpreterBackend>();
        break;
    case CpuBackendKind::Aarch64Jit:
#if defined(ARCH_ARM64)
        g_backend = std::make_unique<Aarch64JitBackend>();
#else
        LOG_ERROR(Core_Cpu, "Aarch64Jit backend not available on this architecture; "
                  "falling back to interpreter");
        g_backend = std::make_unique<X64InterpreterBackend>();
        g_selected_kind = CpuBackendKind::X64Interpreter;
#endif
        break;
    default:
        LOG_ERROR(Core_Cpu, "Unknown backend kind {}; falling back to interpreter",
                  static_cast<unsigned>(kind));
        g_backend = std::make_unique<X64InterpreterBackend>();
        g_selected_kind = CpuBackendKind::X64Interpreter;
        break;
    }
}

CpuBackend* GetBackend() {
    if (!g_backend) {
        SetBackendKind(CpuBackendKind::Auto);
    }
    return g_backend.get();
}

void ConfigureFromSettings() {
    if (g_selected_kind == CpuBackendKind::Auto) {
        SetBackendKind(CpuBackendKind::Auto);
    }
}

} // namespace Core::Cpu

// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Backend selection: ResolveAutoBackend() picks a backend based on the
// host architecture at startup; SetBackendKind() lets the settings UI
// force a different one. GetBackend() returns the currently active
// singleton — module.cpp / linker.cpp call this from Module::Start to
// hand off the entry-point execution to whichever backend is selected.

#include "core/cpu/cpu_backend.h"

#include <cstdlib>
#include <memory>

#include "common/logging/log.h"
#include "core/cpu/interpreter/x64_interpreter_backend.h"
#include "core/emulator_settings.h"

#if defined(ARCH_X86_64)
#include "core/cpu/native/native_x64_backend.h"
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
    // x86-64 host (e.g. Linux/Windows desktop, Apple-Silicon Mac under
    // Rosetta): just jump to the guest code. This is what upstream
    // shadPS4 does on x86-64.
    return CpuBackendKind::NativeX64;
#elif defined(ARCH_ARM64)
    // ARM64 host (Android, Apple Silicon without Rosetta, ARM Linux):
    // start with the interpreter; the JIT will be selected automatically
    // once SetBackendKind is called from the settings UI and the JIT
    // backend is registered.
    return CpuBackendKind::X64Interpreter;
#else
    #error "Unsupported host architecture for Core::Cpu — need either ARCH_X86_64 or ARCH_ARM64"
#endif
}

CpuBackendKind GetSelectedBackendKind() {
    return g_selected_kind;
}

void SetBackendKind(CpuBackendKind kind) {
    if (kind == g_selected_kind && g_backend) {
        return; // no-op
    }
    if (kind == CpuBackendKind::Auto) {
        kind = ResolveAutoBackend();
    }
    LOG_INFO(Core_Cpu, "Switching CPU backend: {} -> {}", CpuBackendKindToString(g_selected_kind),
             CpuBackendKindToString(kind));
    g_selected_kind = kind;
    // Re-create the backend.
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
        LOG_ERROR(Core_Cpu, "Aarch64Jit backend not yet available; falling back to interpreter");
        g_backend = std::make_unique<X64InterpreterBackend>();
        g_selected_kind = CpuBackendKind::X64Interpreter;
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
        // Lazy-initialize with the auto-resolved backend. This is the
        // path taken on first launch when the settings UI hasn't picked
        // anything yet.
        SetBackendKind(CpuBackendKind::Auto);
    }
    return g_backend.get();
}

void ConfigureFromSettings() {
    // Pick a sensible default based on host architecture if the user
    // hasn't set anything in the settings UI yet.
    if (g_selected_kind == CpuBackendKind::Auto) {
        SetBackendKind(CpuBackendKind::Auto);
    }
}

} // namespace Core::Cpu

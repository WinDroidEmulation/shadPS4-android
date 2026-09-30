// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NativeX64Backend — direct execution on an x86-64 host. This is what
// upstream shadPS4 does on x86-64 Linux/Windows: the loader maps the
// guest ELF segments into the host process address space, then the
// entry point is called as a regular function pointer. No translation
// is needed because guest and host share the ISA.
//
// On ARM64 this backend is never selected (ResolveAutoBackend returns
// X64Interpreter instead). We still compile the file to keep the
// CpuBackendKind::NativeX64 dispatch target linkable; if the user
// somehow forces it via SetBackendKind the constructor will assert
// with a clear message.

#pragma once

#include "core/cpu/cpu_backend.h"

namespace Core::Cpu {

class NativeX64Backend : public CpuBackend {
public:
    NativeX64Backend() = default;
    ~NativeX64Backend() override = default;

    u64 Execute(u64 rip, const GuestCallContext& ctx) override;
    CpuBackendKind Kind() const override { return CpuBackendKind::NativeX64; }
};

} // namespace Core::Cpu

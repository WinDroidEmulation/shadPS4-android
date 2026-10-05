// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Aarch64JitBackend — x86-64 → ARM64 JIT compiler.
//
// Execution model:
//   Execute(rip, ctx) looks up `rip` in the block cache. If a translated
//   ARM64 block exists, it jumps to it. If not, it translates the x86-64
//   basic block starting at `rip` into ARM64 code, caches it, and runs it.
//
//   The translated ARM64 code directly accesses guest memory (guest VA =
//   host VA identity mapping) and reads/writes the X64CpuState struct.
//   When it hits an instruction it can't translate, it emits a trampoline
//   that calls back into the interpreter for that single instruction, then
//   re-enters the JIT at the next instruction.
//
//   On block exit (non-local jump, RET to host, HLE call), the translated
//   code writes the new RIP into X64CpuState::rip and returns.

#pragma once

#include <memory>

#include "core/cpu/cpu_backend.h"
#include "core/cpu/jit/jit_block_cache.h"

namespace Core::Cpu {

class Aarch64JitBackend : public CpuBackend {
public:
    Aarch64JitBackend();
    ~Aarch64JitBackend() override;

    u64 Execute(u64 rip, const GuestCallContext& ctx) override;
    CpuBackendKind Kind() const override { return CpuBackendKind::Aarch64Jit; }

    void SetRuntimeConfig(const std::string& key, const std::string& value) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Core::Cpu

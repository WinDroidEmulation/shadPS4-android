// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// x86-64 CPU state structure used by all translation backends. Mirrors the
// register file of an Intel/AMD CPU in long mode: 16 GPRs (RAX-R15), 16 XMM
// regs (XMM0-XMM15), the RFLAGS word, RIP, and the segment base registers
// we actually use in long mode (FS = TLS base, GS = kernel per-CPU base on
// real hardware, here just zero-extended scratch).
//
// Field layout MUST stay binary-stable: JIT backends generate code that
// reads/writes these fields by offset, and the interpreter reads them by
// name.  Use C-style fixed-layout types (uint64_t, uint8_t) — no
// std::array<…> at the top level; that lets us take the address of any
// field with offsetof() and pass the state across the native↔translated
// boundary without surprises.

#pragma once

#include <cstdint>

#include "common/types.h"

namespace Core::Cpu {

// 16 general-purpose registers, in Zydis order (ZYDIS_REGISTER_RAX..R15).
// We store them as raw 64-bit values; sub-register access (AL/AH/AX/EAX)
// is done by ReadReg/WriteReg helpers that know the Zydis register codes.
struct X64CpuState {
    u64 gpr[16] = {};       // RAX RCX RDX RBX RSP RBP RSI RDI R8..R15
    u64 rip = 0;            // Instruction pointer (guest virtual address)
    u64 rflags = 0;         // Full RFLAGS (CF PF AF ZF SF TF IF DF OF NT)

    // 16 XMM/YMM/ZMM registers. PS4 OS / games only use XMM (128-bit), but
    // we keep the storage at 256-bit for the SSE→AVX bridge and future
    // AVX/AVX2 support without an ABI break.
    u8 xmm[16][32] = {{}};

    // Segment base registers (we only really use FS/GS in long mode).
    u64 fs_base = 0;
    u64 gs_base = 0;
    u64 cs_base = 0;
    u64 ds_base = 0;
    u64 es_base = 0;
    u64 ss_base = 0;

    // Per-thread flag indicating whether this state belongs to a guest
    // thread that's currently executing translated code.  The signal
    // handler checks this to decide whether to dispatch the fault back
    // into the interpreter/JIT recovery path or to treat it as a host
    // crash.
    bool in_guest_code = false;

    // MXCSR — SSE control/status register. Defaults match Intel reset
    // values (FZ=0, DAZ=0, rounding=nearest, exceptions all masked).
    u32 mxcsr = 0x1F80;
};

// RFLAGS bit positions (Intel SDM Vol. 1, Table 3-7).
// We use these as bit indexes into X64CpuState::rflags.
namespace RflagsBits {
constexpr u64 CF = 1ULL << 0;   // bit 0  — Carry
constexpr u64 PF = 1ULL << 2;   // bit 2  — Parity
constexpr u64 AF = 1ULL << 4;   // bit 4  — Auxiliary carry
constexpr u64 ZF = 1ULL << 6;   // bit 6  — Zero
constexpr u64 SF = 1ULL << 7;   // bit 7  — Sign
constexpr u64 TF = 1ULL << 8;   // bit 8  — Trap (single-step)
constexpr u64 IF = 1ULL << 9;   // bit 9  — Interrupt enable
constexpr u64 DF = 1ULL << 10;  // bit 10 — Direction
constexpr u64 OF = 1ULL << 11;  // bit 11 — Overflow
constexpr u64 NT = 1ULL << 14;  // bit 14 — Nested task
constexpr u64 RF = 1ULL << 16;  // bit 16 — Resume
constexpr u64 VM = 1ULL << 17;  // bit 17 — Virtual-8086
constexpr u64 AC = 1ULL << 18;  // bit 18 — Alignment check
constexpr u64 VIF = 1ULL << 19; // bit 19 — Virtual interrupt flag
constexpr u64 VIP = 1ULL << 20; // bit 20 — Virtual interrupt pending
constexpr u64 ID = 1ULL << 21;  // bit 21 — CPUID
} // namespace RflagsBits

// Indices into X64CpuState::gpr[] — match Zydis register ordering for
// RAX..R15 so we can translate a ZydisRegister_ into an index with a
// simple subtract.
enum GprIndex : u8 {
    GPR_RAX = 0,
    GPR_RCX = 1,
    GPR_RDX = 2,
    GPR_RBX = 3,
    GPR_RSP = 4,
    GPR_RBP = 5,
    GPR_RSI = 6,
    GPR_RDI = 7,
    GPR_R8  = 8,
    GPR_R9  = 9,
    GPR_R10 = 10,
    GPR_R11 = 11,
    GPR_R12 = 12,
    GPR_R13 = 13,
    GPR_R14 = 14,
    GPR_R15 = 15,
};

} // namespace Core::Cpu

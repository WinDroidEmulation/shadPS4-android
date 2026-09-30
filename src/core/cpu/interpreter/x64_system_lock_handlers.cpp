// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// System / synchronization / control-flow-misc instruction handlers for
// the x86-64 interpreter. Covers:
//   - SYSCALL / INT3 / UD2 / HLT / CPUID / RDTSC / RDTSCP / WBINVD / PAUSE
//   - LOCK prefix variants (LOCK ADD / SUB / AND / OR / XOR / INC / DEC /
//     NEG / NOT / XADD / CMPXCHG / XCHG / DEC / BTC / BTS / BTR / BTC)
//   - CLC / STC / CMC / CLD / STD / CLTS / CLAC / STAC
//   - LEAVE / ENTER / PUSHF / POPF / PUSHFQ / POPFQ / PUSHFD / POPFD
//   - LAHF / SAHF / RDTSCP / ENDBR / NOP-with-prefix
//
// Most of these are HLE-bridged or no-ops in our interpreter because the
// guest's "syscall" calls go through shadPS4's HLE layer rather than a
// real kernel. SYSCALL here just hits the "unimplemented syscall" log
// path which lets the guest continue (the PS4 libkernel's SYSCALL calls
// are stubbed out at the shadPS4 HLE level).

#include "core/cpu/interpreter/x64_interpreter_backend.h"

#include <Zydis/Zydis.h>
#include <Zydis/Decoder.h>
#include <Zydis/DecoderTypes.h>
#include <Zydis/Mnemonic.h>
#include <Zydis/Register.h>

#include <chrono>
#include <cstring>

#include "common/logging/log.h"
#include "core/cpu/x64_cpu_state.h"

namespace Core::Cpu {

namespace {

inline void SetFlag(X64CpuState& s, u64 mask, bool on) {
    if (on) s.rflags |= mask;
    else    s.rflags &= ~mask;
}

inline bool GetFlag(const X64CpuState& s, u64 mask) {
    return (s.rflags & mask) != 0;
}

inline u64 OperandMask(u32 bits) {
    return bits == 64 ? ~0ULL : (1ULL << bits) - 1;
}

// CPUID for the PS4's emulated CPU. PS4 libkernel checks for the CPU
// signature in CPUID(0). We return a custom signature that matches the
// AMD "Jaguar" cores the PS4 uses (CPUID 0x630f01). This is what the
// real PS4 reports and what shadPS4's HLE layer assumes.
struct CpuidResult { u32 eax, ebx, ecx, edx; };

CpuidResult DoCpuid(u32 leaf, u32 subleaf) {
    switch (leaf) {
    case 0: // Vendor and max leaf
        return {4, 0x68747541 /* "Auth" */, 0x444D4163 /* "cAMD" */,
                0x69746E65 /* "enti" */};
    case 1: { // Family / model / stepping + features
        // AMD Jaguar (PS4): family 0x16 (extended), model 0x30,
        // stepping 0x01 → 0x630f01.
        const u32 base = 0x630f01;
        // Feature flags: SSE/SSE2/SSE3/SSSE3/SSE4.1/SSE4.2/AVX/etc.
        // We pretend to have everything since we're emulating.
        const u32 ecx = (1u << 0)  // SSE3
                     | (1u << 1)  // PCLMULQDQ
                     | (1u << 9)  // SSSE3
                     | (1u << 12) // FMA
                     | (1u << 19) // SSE4.1
                     | (1u << 20) // SSE4.2
                     | (1u << 22) // MOVBE
                     | (1u << 23) // POPCNT
                     | (1u << 25) // AESNI
                     | (1u << 28) // AVX
                     | (1u << 29) // F16C
                     | (1u << 30) // RDRAND
                     ;
        const u32 edx = (1u << 0)  // FPU
                     | (1u << 1)  // VME
                     | (1u << 3)  // PSE
                     | (1u << 4)  // TSC
                     | (1u << 5)  // MSR
                     | (1u << 8)  // CMPXCHG8B
                     | (1u << 11) // SYSENTER/SYSEXIT
                     | (1u << 15) // CMOV
                     | (1u << 19) // CLFLUSH
                     | (1u << 23) // MMX
                     | (1u << 24) // FXSR
                     | (1u << 25) // SSE
                     | (1u << 26) // SSE2
                     ;
        return {base, 0, ecx, edx};
    }
    case 2: case 3: case 4: case 5: case 6:
        // Cache / TLB info — return zero (not used by shadPS4 init).
        return {0, 0, 0, 0};
    case 7: { // Extended features (only subleaf 0)
        if (subleaf == 0) {
            // EBX: BMI1/BMI2/AVX2/etc.
            const u32 ebx = (1u << 3)  // BMI1
                          | (1u << 5)  // AVX2
                          | (1u << 8)  // BMI2
                          ;
            return {0, ebx, 0, 0};
        }
        return {0, 0, 0, 0};
    }
    case 0x80000000: // Extended max leaf
        return {0x8000001F, 0, 0, 0};
    case 0x80000001: { // Extended features
        // EDX: 1G pages, RDTSCP, etc.
        const u32 edx = (1u << 20) // NX bit
                     | (1u << 27) // RDTSCP
                     | (1u << 26) // 1G pages
                     | (1u << 29) // Long mode
                     ;
        return {0, 0, 0, edx};
    }
    default:
        return {0, 0, 0, 0};
    }
}

} // namespace

bool HandleSystemLockInstruction(X64CpuState& s,
                                 const ZydisDecodedInstruction& inst,
                                 const ZydisDecodedOperand* operands) {
    const ZydisMnemonic mn = inst.mnemonic;
    const bool has_lock = (inst.attributes & ZYDIS_ATTRIB_HAS_LOCK) != 0;
    const u32 op_bits = operands[0].size;
    const u64 mask = OperandMask(op_bits);

    switch (mn) {
    // ── Flag-control ─────────────────────────────────────────────────────
    case ZYDIS_MNEMONIC_CLC:  SetFlag(s, RflagsBits::CF, false); return true;
    case ZYDIS_MNEMONIC_STC:  SetFlag(s, RflagsBits::CF, true);  return true;
    case ZYDIS_MNEMONIC_CMC:  s.rflags ^= RflagsBits::CF;        return true;
    case ZYDIS_MNEMONIC_CLD:  SetFlag(s, RflagsBits::DF, false); return true;
    case ZYDIS_MNEMONIC_STD:  SetFlag(s, RflagsBits::DF, true);  return true;
    case ZYDIS_MNEMONIC_CLI:  SetFlag(s, RflagsBits::IF, false); return true;
    case ZYDIS_MNEMONIC_STI:  SetFlag(s, RflagsBits::IF, true);  return true;
    case ZYDIS_MNEMONIC_CLAC: case ZYDIS_MNEMONIC_STAC:
    case ZYDIS_MNEMONIC_CLTS: return true; // no-op
    case ZYDIS_MNEMONIC_SAHF: {
        // SAHF: load AH into the low 8 bits of RFLAGS.
        const u64 ah = (s.gpr[GPR_RAX] >> 8) & 0xFF;
        s.rflags = (s.rflags & ~0xFFULL) | ah;
        // Restore reserved bits 1 and 3 to their fixed values (both 1).
        s.rflags |= (1ULL << 1);
        return true;
    }
    case ZYDIS_MNEMONIC_LAHF: {
        // LAHF: load low 8 bits of RFLAGS into AH.
        const u64 low8 = s.rflags & 0xFF;
        s.gpr[GPR_RAX] = (s.gpr[GPR_RAX] & ~0xFF00ULL) | (low8 << 8);
        return true;
    }
    case ZYDIS_MNEMONIC_PUSHF: case ZYDIS_MNEMONIC_PUSHFQ:
    case ZYDIS_MNEMONIC_PUSHFD: {
        // PUSHF: push RFLAGS low 16/32/64.
        const u32 sz = (mn == ZYDIS_MNEMONIC_PUSHF) ? 2
                     : (mn == ZYDIS_MNEMONIC_PUSHFD) ? 4 : 8;
        s.gpr[GPR_RSP] -= sz;
        WriteMemory(s.gpr[GPR_RSP], s.rflags & OperandMask(sz * 8), sz, false);
        return true;
    }
    case ZYDIS_MNEMONIC_POPF: case ZYDIS_MNEMONIC_POPFQ:
    case ZYDIS_MNEMONIC_POPFD: {
        const u32 sz = (mn == ZYDIS_MNEMONIC_POPF) ? 2
                     : (mn == ZYDIS_MNEMONIC_POPFD) ? 4 : 8;
        const u64 v = ReadMemory(s.gpr[GPR_RSP], sz, false);
        s.rflags = (s.rflags & ~OperandMask(sz * 8)) | v;
        // Restore fixed reserved bits.
        s.rflags |= (1ULL << 1);
        s.gpr[GPR_RSP] += sz;
        return true;
    }

    // ── Stack frame helpers ──────────────────────────────────────────────
    case ZYDIS_MNEMONIC_LEAVE: {
        // LEAVE: RSP = RBP; POP RBP.
        s.gpr[GPR_RSP] = s.gpr[GPR_RBP];
        s.gpr[GPR_RBP] = ReadMemory(s.gpr[GPR_RSP], 8, false);
        s.gpr[GPR_RSP] += 8;
        return true;
    }
    case ZYDIS_MNEMONIC_ENTER: {
        // ENTER imm16, imm8 — allocates a stack frame of size imm16
        // and nests imm8 levels deep. PS4 libkernel rarely uses this;
        // we implement the level=0 form (which is equivalent to
        // PUSH RBP; MOV RBP, RSP; SUB RSP, imm16).
        const u16 size = static_cast<u16>(operands[0].imm.value.u);
        const u8 nesting = static_cast<u8>(operands[1].imm.value.u);
        s.gpr[GPR_RSP] -= 8;
        WriteMemory(s.gpr[GPR_RSP], s.gpr[GPR_RBP], 8, false);
        s.gpr[GPR_RBP] = s.gpr[GPR_RSP];
        if (nesting == 0) {
            s.gpr[GPR_RSP] -= size;
        } else {
            // For nesting > 0 we'd need to push nesting-1 frame pointers
            // from outer frames. PS4 libkernel init doesn't use this, so
            // we fall back to the level=0 form.
            s.gpr[GPR_RSP] -= size;
            LOG_WARNING(Core_Cpu, "ENTER with nesting={} not fully implemented", nesting);
        }
        return true;
    }

    // ── CPU info / timing ───────────────────────────────────────────────
    case ZYDIS_MNEMONIC_CPUID: {
        const u32 leaf = static_cast<u32>(s.gpr[GPR_RAX]);
        const u32 subleaf = static_cast<u32>(s.gpr[GPR_RCX]);
        const CpuidResult r = DoCpuid(leaf, subleaf);
        s.gpr[GPR_RAX] = r.eax;
        s.gpr[GPR_RBX] = r.ebx;
        s.gpr[GPR_RCX] = r.ecx;
        s.gpr[GPR_RDX] = r.edx;
        return true;
    }
    case ZYDIS_MNEMONIC_RDTSC: case ZYDIS_MNEMONIC_RDTSCP: {
        // Read time stamp counter. We return host clock monotonic
        // nanoseconds split into EDX:EAX. RDTSCP also writes the
        // "aux" MSR into ECX (we use 0).
        const auto now = std::chrono::steady_clock::now();
        const u64 tsc = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count());
        s.gpr[GPR_RAX] = tsc & 0xFFFF'FFFF'FFFF'FFFFULL;
        s.gpr[GPR_RDX] = 0; // host nanoseconds fit in 64 bits → high 64 = 0
        if (mn == ZYDIS_MNEMONIC_RDTSCP) s.gpr[GPR_RCX] = 0;
        return true;
    }
    case ZYDIS_MNEMONIC_RDMSR: case ZYDIS_MNEMONIC_WRMSR:
    case ZYDIS_MNEMONIC_WBINVD: case ZYDIS_MNEMONIC_INVD:
    case ZYDIS_MNEMONIC_PAUSE:
    case ZYDIS_MNEMONIC_HLT: case ZYDIS_MNEMONIC_MONITOR:
    case ZYDIS_MNEMONIC_MWAIT:
        return true; // no-op (we're cooperative scheduling)

    // ── SYSCALL / INT3 / UD2 ─────────────────────────────────────────────
    case ZYDIS_MNEMONIC_SYSCALL: case ZYDIS_MNEMONIC_SYSENTER: {
        // SYSCALL on PS4: the guest calls into libkernel which dispatches
        // to an HLE implementation in shadPS4. The kernel's syscall
        // numbers are loaded from RCX/R10 (depending on the
        // instruction). For SYSCALL specifically the syscall number is
        // in RAX, args in RDI/RSI/RDX/R10/R8/R9, return address in RCX,
        // return RFLAGS in R11.
        // We don't dispatch here directly because shadPS4 patches
        // SYSCALL to call into its HLE function table at module load
        // time (see src/core/cpu_patches.cpp). So if we reach here it's
        // either an unpatched syscall or an unsupported one.
        LOG_WARNING(Core_Cpu,
                    "Unpatched {} at rip=0x{:016x}, rax=0x{:x} — treating as no-op",
                    (mn == ZYDIS_MNEMONIC_SYSCALL ? "SYSCALL" : "SYSENTER"),
                    s.rip, s.gpr[GPR_RAX]);
        return true;
    }
    case ZYDIS_MNEMONIC_SYSEXIT:
        return true; // no-op
    case ZYDIS_MNEMONIC_INT3:
    case ZYDIS_MNEMONIC_INT1:
    case ZYDIS_MNEMONIC_INTO: {
        LOG_WARNING(Core_Cpu, "INT/INT3/INTO at rip=0x{:016x}", s.rip);
        return true;
    }
    case ZYDIS_MNEMONIC_UD2: {
        LOG_ERROR(Core_Cpu, "UD2 (undefined instruction) at rip=0x{:016x}", s.rip);
        // On real hardware UD2 raises #UD. We treat as no-op and let
        // the guest continue (PS4 sometimes uses UD2 as a panic
        // indicator and continues after).
        return true;
    }

    // ── LOCK-prefixed forms ──────────────────────────────────────────────
    // This version of Zydis emits the *unprefixed* mnemonic even when
    // the LOCK prefix is set — the prefix itself is recorded in
    // inst.attributes (ZYDIS_ATTRIB_HAS_LOCK). So we have to check the
    // attribute at the integer-handler level rather than separate
    // ZYDIS_MNEMONIC_LOCK_* mnemonics. We handle the most common
    // LOCK-prefixed ops here.
    case ZYDIS_MNEMONIC_ADD: case ZYDIS_MNEMONIC_SUB:
    case ZYDIS_MNEMONIC_AND: case ZYDIS_MNEMONIC_OR:
    case ZYDIS_MNEMONIC_XOR: case ZYDIS_MNEMONIC_INC:
    case ZYDIS_MNEMONIC_DEC: case ZYDIS_MNEMONIC_NEG:
    case ZYDIS_MNEMONIC_NOT: case ZYDIS_MNEMONIC_XADD:
    case ZYDIS_MNEMONIC_CMPXCHG: case ZYDIS_MNEMONIC_XCHG:
    case ZYDIS_MNEMONIC_BT: case ZYDIS_MNEMONIC_BTS:
    case ZYDIS_MNEMONIC_BTR: case ZYDIS_MNEMONIC_BTC:
    case ZYDIS_MNEMONIC_CMPXCHG8B: case ZYDIS_MNEMONIC_CMPXCHG16B: {
        // Only handle here when LOCK is set; otherwise let the integer
        // dispatcher take it.
        if (!has_lock && mn != ZYDIS_MNEMONIC_CMPXCHG8B &&
            mn != ZYDIS_MNEMONIC_CMPXCHG16B) {
            return false;
        }
        // CMPXCHG8B/16B is only interesting when LOCK is set; Zydis
        // emits the bare CMPXCHG8B mnemonic even without LOCK.
        if (mn == ZYDIS_MNEMONIC_ADD || mn == ZYDIS_MNEMONIC_SUB ||
            mn == ZYDIS_MNEMONIC_AND || mn == ZYDIS_MNEMONIC_OR ||
            mn == ZYDIS_MNEMONIC_XOR) {
            const u64 lhs = ReadOperand(s, inst, operands[0], false);
            const u64 rhs = ReadOperand(s, inst, operands[1], false);
            u64 r;
            if (mn == ZYDIS_MNEMONIC_ADD)      r = lhs + rhs;
            else if (mn == ZYDIS_MNEMONIC_SUB) r = lhs - rhs;
            else if (mn == ZYDIS_MNEMONIC_AND) r = lhs & rhs;
            else if (mn == ZYDIS_MNEMONIC_OR)  r = lhs | rhs;
            else                                r = lhs ^ rhs;
            r &= mask;
            WriteOperand(s, inst, operands[0], r, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_INC) {
            const u64 lhs = ReadOperand(s, inst, operands[0], false);
            WriteOperand(s, inst, operands[0], (lhs + 1) & mask, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_DEC) {
            const u64 lhs = ReadOperand(s, inst, operands[0], false);
            WriteOperand(s, inst, operands[0], (lhs - 1) & mask, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_NEG) {
            const u64 lhs = ReadOperand(s, inst, operands[0], false);
            WriteOperand(s, inst, operands[0], (~lhs + 1) & mask, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_NOT) {
            const u64 lhs = ReadOperand(s, inst, operands[0], false);
            WriteOperand(s, inst, operands[0], ~lhs & mask, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_XADD) {
            const u64 a = ReadOperand(s, inst, operands[0], false);
            const u64 b = ReadOperand(s, inst, operands[1], false);
            WriteOperand(s, inst, operands[0], (a + b) & mask, false);
            WriteOperand(s, inst, operands[1], a, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_CMPXCHG) {
            const u64 acc = GetRegValue(s, ZYDIS_REGISTER_RAX, op_bits / 8);
            const u64 dst = ReadOperand(s, inst, operands[0], false);
            const u64 src = ReadOperand(s, inst, operands[1], false);
            if (acc == dst) {
                WriteOperand(s, inst, operands[0], src, false);
                SetFlag(s, RflagsBits::ZF, true);
            } else {
                SetRegValue(s, ZYDIS_REGISTER_RAX, dst, op_bits / 8);
                SetFlag(s, RflagsBits::ZF, false);
            }
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_XCHG) {
            const u64 a = ReadOperand(s, inst, operands[0], false);
            const u64 b = ReadOperand(s, inst, operands[1], false);
            WriteOperand(s, inst, operands[0], b, false);
            WriteOperand(s, inst, operands[1], a, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_BT) {
            const u64 v = ReadOperand(s, inst, operands[0], false);
            const u64 bit = ReadOperand(s, inst, operands[1], false) % op_bits;
            SetFlag(s, RflagsBits::CF, (v >> bit) & 1);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_BTS || mn == ZYDIS_MNEMONIC_BTR ||
            mn == ZYDIS_MNEMONIC_BTC) {
            u64 v = ReadOperand(s, inst, operands[0], false);
            const u64 bit = ReadOperand(s, inst, operands[1], false) % op_bits;
            SetFlag(s, RflagsBits::CF, (v >> bit) & 1);
            if (mn == ZYDIS_MNEMONIC_BTS) v |= (1ULL << bit);
            else if (mn == ZYDIS_MNEMONIC_BTR) v &= ~(1ULL << bit);
            else v ^= (1ULL << bit);
            WriteOperand(s, inst, operands[0], v, false);
            return true;
        }
        if (mn == ZYDIS_MNEMONIC_CMPXCHG8B) {
            const u64 addr = GetRegValue(s, operands[0].mem.base, 8) +
                             (operands[0].mem.index != ZYDIS_REGISTER_NONE
                                  ? GetRegValue(s, operands[0].mem.index, 8) *
                                        (1ULL << operands[0].mem.scale)
                                  : 0) +
                             static_cast<u64>(static_cast<s64>(operands[0].mem.disp.value));
            const u64 current = ReadMemory(addr, 8, false);
            const u64 acc = (s.gpr[GPR_RDX] << 32) |
                            (s.gpr[GPR_RAX] & 0xFFFF'FFFFULL);
            if (current == acc) {
                const u64 new_v = ((s.gpr[GPR_RCX] & 0xFFFF'FFFFULL) << 32) |
                                   (s.gpr[GPR_RBX] & 0xFFFF'FFFFULL);
                WriteMemory(addr, new_v, 8, false);
                SetFlag(s, RflagsBits::ZF, true);
            } else {
                s.gpr[GPR_RAX] = current & 0xFFFF'FFFFULL;
                s.gpr[GPR_RDX] = (current >> 32) & 0xFFFF'FFFFULL;
                SetFlag(s, RflagsBits::ZF, false);
            }
            return true;
        }
        // CMPXCHG16B
        const u64 addr = GetRegValue(s, operands[0].mem.base, 8) +
                         (operands[0].mem.index != ZYDIS_REGISTER_NONE
                              ? GetRegValue(s, operands[0].mem.index, 8) *
                                    (1ULL << operands[0].mem.scale)
                              : 0) +
                         static_cast<u64>(static_cast<s64>(operands[0].mem.disp.value));
        const u64 lo = ReadMemory(addr, 8, false);
        const u64 hi = ReadMemory(addr + 8, 8, false);
        const u64 acc_lo = s.gpr[GPR_RAX];
        const u64 acc_hi = s.gpr[GPR_RDX];
        if (lo == acc_lo && hi == acc_hi) {
            const u64 new_lo = s.gpr[GPR_RBX];
            const u64 new_hi = s.gpr[GPR_RCX];
            WriteMemory(addr, new_lo, 8, false);
            WriteMemory(addr + 8, new_hi, 8, false);
            SetFlag(s, RflagsBits::ZF, true);
        } else {
            s.gpr[GPR_RAX] = lo;
            s.gpr[GPR_RDX] = hi;
            SetFlag(s, RflagsBits::ZF, false);
        }
        return true;
    }

    // ── Misc unhandled — caller's default ───────────────────────────────
    default:
        return false;
    }
}

} // namespace Core::Cpu

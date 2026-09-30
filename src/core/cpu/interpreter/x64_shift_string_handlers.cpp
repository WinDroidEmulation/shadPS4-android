// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shift / rotate / bit-test / bit-scan + string operations (MOVS/STOS/
// LODS/CMPS/SCAS) with REP prefix support.
//
// Shift/rotate/bit-test/bit-scan are the integer instructions the
// interpreter dispatches after the SSE dispatcher returns false.
// The string ops + REP prefix form their own dispatcher because
// they need the prefix bits from the decoded instruction and are
// awkward to express as single-instruction handlers.

#include "core/cpu/interpreter/x64_interpreter_backend.h"

#include <Zydis/Zydis.h>
#include <Zydis/Decoder.h>
#include <Zydis/DecoderTypes.h>
#include <Zydis/Mnemonic.h>
#include <Zydis/Register.h>

#include <cstring>

#include "common/logging/log.h"
#include "core/cpu/x64_cpu_state.h"

namespace Core::Cpu {

// ─────────────────────────────────────────────────────────────────────────
//  Helpers shared with the integer dispatcher
// ─────────────────────────────────────────────────────────────────────────

namespace {

inline u64 OperandMask(u32 bits) {
    return bits == 64 ? ~0ULL : (1ULL << bits) - 1;
}

inline u64 SignBit(u32 bits) {
    return bits == 64 ? (1ULL << 63) : (1ULL << (bits - 1));
}

inline void SetFlag(X64CpuState& s, u64 mask, bool on) {
    if (on) s.rflags |= mask;
    else    s.rflags &= ~mask;
}

inline bool GetFlag(const X64CpuState& s, u64 mask) {
    return (s.rflags & mask) != 0;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
//  Shift / rotate / bit-test / bit-scan dispatcher
//  Returns true if the mnemonic was handled.
// ─────────────────────────────────────────────────────────────────────────

bool HandleShiftBitInstruction(X64CpuState& s,
                               const ZydisDecodedInstruction& inst,
                               const ZydisDecodedOperand* operands) {
    const ZydisMnemonic mn = inst.mnemonic;
    const u32 op_bits = operands[0].size;
    const u32 mask = OperandMask(op_bits);
    const u64 sign = SignBit(op_bits);

    auto read_count = [&]() -> u64 {
        // Count is in operands[1]; can be immediate or CL.
        return ReadOperand(s, inst, operands[1], false) & 0x3F;
    };
    auto update_logic = [&](u64 r) {
        SetFlag(s, RflagsBits::CF, false);
        SetFlag(s, RflagsBits::OF, false);
        SetFlag(s, RflagsBits::PF, false);
        SetFlag(s, RflagsBits::AF, false);
        SetFlag(s, RflagsBits::ZF, (r & mask) == 0);
        SetFlag(s, RflagsBits::SF, (r & sign) != 0);
    };

    switch (mn) {
    case ZYDIS_MNEMONIC_ROL: {
        const u64 v = ReadOperand(s, inst, operands[0], false);
        const u64 c = read_count();
        if (c == 0) return true;
        const u64 effective = c % op_bits;
        const u64 r = ((v << effective) | (v >> (op_bits - effective))) & mask;
        WriteOperand(s, inst, operands[0], r, false);
        if (effective) SetFlag(s, RflagsBits::CF, r & 1);
        return true;
    }
    case ZYDIS_MNEMONIC_ROR: {
        const u64 v = ReadOperand(s, inst, operands[0], false);
        const u64 c = read_count();
        if (c == 0) return true;
        const u64 effective = c % op_bits;
        const u64 r = ((v >> effective) | (v << (op_bits - effective))) & mask;
        WriteOperand(s, inst, operands[0], r, false);
        if (effective) SetFlag(s, RflagsBits::CF, (r >> (op_bits - 1)) & 1);
        return true;
    }
    case ZYDIS_MNEMONIC_RCL: {
        const u64 v = ReadOperand(s, inst, operands[0], false);
        const u64 c = read_count();
        if (c == 0) return true;
        // RCL includes CF in the rotation (op_bits + 1 bits total).
        bool carry = GetFlag(s, RflagsBits::CF);
        u64 r = v;
        for (u64 i = 0; i < (c % (op_bits + 1)); ++i) {
            const bool new_carry = (r >> (op_bits - 1)) & 1;
            r = ((r << 1) | (carry ? 1 : 0)) & mask;
            carry = new_carry;
        }
        WriteOperand(s, inst, operands[0], r, false);
        SetFlag(s, RflagsBits::CF, carry);
        return true;
    }
    case ZYDIS_MNEMONIC_RCR: {
        const u64 v = ReadOperand(s, inst, operands[0], false);
        const u64 c = read_count();
        if (c == 0) return true;
        bool carry = GetFlag(s, RflagsBits::CF);
        u64 r = v;
        for (u64 i = 0; i < (c % (op_bits + 1)); ++i) {
            const bool new_carry = r & 1;
            r = (r >> 1) | (carry ? (1ULL << (op_bits - 1)) : 0);
            r &= mask;
            carry = new_carry;
        }
        WriteOperand(s, inst, operands[0], r, false);
        SetFlag(s, RflagsBits::CF, carry);
        return true;
    }
    case ZYDIS_MNEMONIC_SHLD: {
        // SHLD dst, src, count
        const u64 d = ReadOperand(s, inst, operands[0], false);
        const u64 src = ReadOperand(s, inst, operands[1], false);
        const u64 c = read_count();
        if (c == 0) return true;
        const u64 eff = (c >= op_bits) ? op_bits : c;
        u64 r;
        if (op_bits == 64) {
            const __uint128_t wide = (static_cast<__uint128_t>(src) << 64) | d;
            r = static_cast<u64>(wide << eff);
            if (c >= 64) r |= static_cast<u64>(wide >> (128 - c));
            r &= mask;
        } else {
            const u64 wide = (src << op_bits) | d;
            r = (wide << eff) & ((1ULL << (2 * op_bits)) - 1);
            r >>= op_bits - eff; // shift back to position
            r &= mask;
        }
        WriteOperand(s, inst, operands[0], r, false);
        if (c) SetFlag(s, RflagsBits::CF, (d >> (op_bits - eff)) & 1);
        update_logic(r);
        return true;
    }
    case ZYDIS_MNEMONIC_SHRD: {
        const u64 d = ReadOperand(s, inst, operands[0], false);
        const u64 src = ReadOperand(s, inst, operands[1], false);
        const u64 c = read_count();
        if (c == 0) return true;
        const u64 eff = (c >= op_bits) ? op_bits : c;
        u64 r;
        if (op_bits == 64) {
            const __uint128_t wide = (static_cast<__uint128_t>(src) << 64) | d;
            r = static_cast<u64>(wide >> eff);
        } else {
            const u64 wide = (src << op_bits) | d;
            r = (wide >> eff) & mask;
        }
        WriteOperand(s, inst, operands[0], r, false);
        if (c) SetFlag(s, RflagsBits::CF, (d >> (eff - 1)) & 1);
        update_logic(r);
        return true;
    }
    case ZYDIS_MNEMONIC_BT: {
        // BT r/m, r — test bit at position src; sets CF.
        const u64 base = ReadOperand(s, inst, operands[0], false);
        u64 bit;
        if (operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER) {
            // For register operand, bit index modulo operand size.
            bit = ReadOperand(s, inst, operands[1], false) % op_bits;
        } else {
            // For immediate, bit is in the range [0, op_bits).
            bit = ReadOperand(s, inst, operands[1], false) & (op_bits - 1);
        }
        SetFlag(s, RflagsBits::CF, (base >> bit) & 1);
        return true;
    }
    case ZYDIS_MNEMONIC_BTS: {
        u64 base = ReadOperand(s, inst, operands[0], false);
        u64 bit = ReadOperand(s, inst, operands[1], false) % op_bits;
        SetFlag(s, RflagsBits::CF, (base >> bit) & 1);
        base |= (1ULL << bit);
        WriteOperand(s, inst, operands[0], base, false);
        return true;
    }
    case ZYDIS_MNEMONIC_BTR: {
        u64 base = ReadOperand(s, inst, operands[0], false);
        u64 bit = ReadOperand(s, inst, operands[1], false) % op_bits;
        SetFlag(s, RflagsBits::CF, (base >> bit) & 1);
        base &= ~(1ULL << bit);
        WriteOperand(s, inst, operands[0], base, false);
        return true;
    }
    case ZYDIS_MNEMONIC_BTC: {
        u64 base = ReadOperand(s, inst, operands[0], false);
        u64 bit = ReadOperand(s, inst, operands[1], false) % op_bits;
        SetFlag(s, RflagsBits::CF, (base >> bit) & 1);
        base ^= (1ULL << bit);
        WriteOperand(s, inst, operands[0], base, false);
        return true;
    }
    case ZYDIS_MNEMONIC_BSF: {
        // Bit scan forward: find lowest set bit; ZF=1 if src==0.
        const u64 v = ReadOperand(s, inst, operands[1], false);
        if (v == 0) {
            SetFlag(s, RflagsBits::ZF, true);
            return true;
        }
        SetFlag(s, RflagsBits::ZF, false);
        u64 r = 0;
        for (u64 i = 0; i < op_bits; ++i) {
            if ((v >> i) & 1) { r = i; break; }
        }
        WriteOperand(s, inst, operands[0], r, false);
        return true;
    }
    case ZYDIS_MNEMONIC_BSR: {
        const u64 v = ReadOperand(s, inst, operands[1], false);
        if (v == 0) {
            SetFlag(s, RflagsBits::ZF, true);
            return true;
        }
        SetFlag(s, RflagsBits::ZF, false);
        u64 r = 0;
        for (s64 i = op_bits - 1; i >= 0; --i) {
            if ((v >> i) & 1) { r = static_cast<u64>(i); break; }
        }
        WriteOperand(s, inst, operands[0], r, false);
        return true;
    }
    case ZYDIS_MNEMONIC_POPCNT: {
        // POPCNT r, r/m — count set bits. PS4 libkernel uses this for
        // sparse-bitmap scans.
        const u64 v = ReadOperand(s, inst, operands[1], false);
        u64 count = 0;
        u64 x = v & mask;
        while (x) {
            count += x & 1;
            x >>= 1;
        }
        WriteOperand(s, inst, operands[0], count, false);
        SetFlag(s, RflagsBits::ZF, count == 0);
        SetFlag(s, RflagsBits::CF, false);
        SetFlag(s, RflagsBits::OF, false);
        SetFlag(s, RflagsBits::SF, false);
        SetFlag(s, RflagsBits::PF, false);
        SetFlag(s, RflagsBits::AF, false);
        return true;
    }
    case ZYDIS_MNEMONIC_TZCNT: {
        // TZCNT: like BSF but doesn't set ZF if src==0; instead returns
        // op_bits as the index.
        const u64 v = ReadOperand(s, inst, operands[1], false);
        u64 r = op_bits;
        for (u64 i = 0; i < op_bits; ++i) {
            if ((v >> i) & 1) { r = i; break; }
        }
        WriteOperand(s, inst, operands[0], r, false);
        SetFlag(s, RflagsBits::ZF, v == 0);
        SetFlag(s, RflagsBits::CF, v == 0);
        return true;
    }
    case ZYDIS_MNEMONIC_LZCNT: {
        const u64 v = ReadOperand(s, inst, operands[1], false) & mask;
        u64 r = op_bits;
        for (s64 i = op_bits - 1; i >= 0; --i) {
            if ((v >> i) & 1) { r = op_bits - 1 - static_cast<u64>(i); break; }
        }
        WriteOperand(s, inst, operands[0], r, false);
        SetFlag(s, RflagsBits::ZF, v == 0);
        SetFlag(s, RflagsBits::CF, v == 0);
        return true;
    }
    default:
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  String ops + REP prefix dispatcher
//  Returns true if the mnemonic was handled.
// ─────────────────────────────────────────────────────────────────────────

bool HandleStringRepInstruction(X64CpuState& s,
                                const ZydisDecodedInstruction& inst,
                                const ZydisDecodedOperand* /*operands*/) {
    const ZydisMnemonic mn = inst.mnemonic;

    // Element size: 1 (byte), 2 (word), 4 (dword), 8 (qword).
    const u32 sz = (mn == ZYDIS_MNEMONIC_MOVSB || mn == ZYDIS_MNEMONIC_STOSB ||
                    mn == ZYDIS_MNEMONIC_LODSB || mn == ZYDIS_MNEMONIC_CMPSB ||
                    mn == ZYDIS_MNEMONIC_SCASB)
                       ? 1
                   : (mn == ZYDIS_MNEMONIC_MOVSW || mn == ZYDIS_MNEMONIC_STOSW ||
                      mn == ZYDIS_MNEMONIC_LODSW || mn == ZYDIS_MNEMONIC_CMPSW ||
                      mn == ZYDIS_MNEMONIC_SCASW)
                       ? 2
                   : (mn == ZYDIS_MNEMONIC_MOVSD || mn == ZYDIS_MNEMONIC_STOSD ||
                      mn == ZYDIS_MNEMONIC_LODSD || mn == ZYDIS_MNEMONIC_CMPSD ||
                      mn == ZYDIS_MNEMONIC_SCASD)
                       ? 4
                   : (mn == ZYDIS_MNEMONIC_MOVSQ || mn == ZYDIS_MNEMONIC_STOSQ ||
                      mn == ZYDIS_MNEMONIC_LODSQ || mn == ZYDIS_MNEMONIC_CMPSQ ||
                      mn == ZYDIS_MNEMONIC_SCASQ)
                       ? 8
                       : 0;

    if (sz == 0) return false;

    const bool is_string_op = (mn == ZYDIS_MNEMONIC_MOVSB || mn == ZYDIS_MNEMONIC_MOVSW ||
                               mn == ZYDIS_MNEMONIC_MOVSD || mn == ZYDIS_MNEMONIC_MOVSQ ||
                               mn == ZYDIS_MNEMONIC_STOSB || mn == ZYDIS_MNEMONIC_STOSW ||
                               mn == ZYDIS_MNEMONIC_STOSD || mn == ZYDIS_MNEMONIC_STOSQ ||
                               mn == ZYDIS_MNEMONIC_LODSB || mn == ZYDIS_MNEMONIC_LODSW ||
                               mn == ZYDIS_MNEMONIC_LODSD || mn == ZYDIS_MNEMONIC_LODSQ ||
                               mn == ZYDIS_MNEMONIC_CMPSB || mn == ZYDIS_MNEMONIC_CMPSW ||
                               mn == ZYDIS_MNEMONIC_CMPSD || mn == ZYDIS_MNEMONIC_CMPSQ ||
                               mn == ZYDIS_MNEMONIC_SCASB || mn == ZYDIS_MNEMONIC_SCASW ||
                               mn == ZYDIS_MNEMONIC_SCASD || mn == ZYDIS_MNEMONIC_SCASQ);
    if (!is_string_op) return false;

    // DF: 1 = decrement addresses (CLD sets DF=0, STD sets DF=1).
    const s64 step = (GetFlag(s, RflagsBits::DF) ? -1 : 1) * static_cast<s64>(sz);

    const bool has_rep = (inst.attributes & ZYDIS_ATTRIB_HAS_REP) != 0;
    const bool has_repne = (inst.attributes & ZYDIS_ATTRIB_HAS_REPNE) != 0;
    const bool has_repe = (inst.attributes & ZYDIS_ATTRIB_HAS_REPE) != 0;

    // Helper lambdas for each string op
    auto do_movs = [&]() {
        // RSI → RDI
        const u64 src_addr = s.gpr[GPR_RSI];
        const u64 dst_addr = s.gpr[GPR_RDI];
        const u64 v = ReadMemory(src_addr, sz, false);
        WriteMemory(dst_addr, v, sz, false);
        s.gpr[GPR_RSI] = static_cast<u64>(static_cast<s64>(src_addr) + step);
        s.gpr[GPR_RDI] = static_cast<u64>(static_cast<s64>(dst_addr) + step);
    };
    auto do_stos = [&]() {
        // RAX → RDI
        const u64 dst_addr = s.gpr[GPR_RDI];
        WriteMemory(dst_addr, GetRegValue(s, ZYDIS_REGISTER_RAX, sz), sz, false);
        s.gpr[GPR_RDI] = static_cast<u64>(static_cast<s64>(dst_addr) + step);
    };
    auto do_lods = [&]() {
        // RSI → RAX
        const u64 src_addr = s.gpr[GPR_RSI];
        const u64 v = ReadMemory(src_addr, sz, false);
        SetRegValue(s, ZYDIS_REGISTER_RAX, v, sz);
        s.gpr[GPR_RSI] = static_cast<u64>(static_cast<s64>(src_addr) + step);
    };
    auto do_cmps = [&]() {
        // RSI cmp RDI; sets flags; advance both.
        const u64 a = ReadMemory(s.gpr[GPR_RSI], sz, false);
        const u64 b = ReadMemory(s.gpr[GPR_RDI], sz, false);
        const u64 r = (a - b) & OperandMask(sz * 8);
        UpdateFlagsSub(s, a, b, a - b, sz * 8);
        s.gpr[GPR_RSI] = static_cast<u64>(static_cast<s64>(s.gpr[GPR_RSI]) + step);
        s.gpr[GPR_RDI] = static_cast<u64>(static_cast<s64>(s.gpr[GPR_RDI]) + step);
        (void)r;
    };
    auto do_scas = [&]() {
        // RAX cmp RDI; sets flags; advance RDI.
        const u64 a = GetRegValue(s, ZYDIS_REGISTER_RAX, sz);
        const u64 b = ReadMemory(s.gpr[GPR_RDI], sz, false);
        UpdateFlagsSub(s, a, b, a - b, sz * 8);
        s.gpr[GPR_RDI] = static_cast<u64>(static_cast<s64>(s.gpr[GPR_RDI]) + step);
    };

    if (!has_rep && !has_repne && !has_repe) {
        // Single iteration
        switch (mn) {
        case ZYDIS_MNEMONIC_MOVSB: case ZYDIS_MNEMONIC_MOVSW:
        case ZYDIS_MNEMONIC_MOVSD: case ZYDIS_MNEMONIC_MOVSQ: do_movs(); break;
        case ZYDIS_MNEMONIC_STOSB: case ZYDIS_MNEMONIC_STOSW:
        case ZYDIS_MNEMONIC_STOSD: case ZYDIS_MNEMONIC_STOSQ: do_stos(); break;
        case ZYDIS_MNEMONIC_LODSB: case ZYDIS_MNEMONIC_LODSW:
        case ZYDIS_MNEMONIC_LODSD: case ZYDIS_MNEMONIC_LODSQ: do_lods(); break;
        case ZYDIS_MNEMONIC_CMPSB: case ZYDIS_MNEMONIC_CMPSW:
        case ZYDIS_MNEMONIC_CMPSD: case ZYDIS_MNEMONIC_CMPSQ: do_cmps(); break;
        case ZYDIS_MNEMONIC_SCASB: case ZYDIS_MNEMONIC_SCASW:
        case ZYDIS_MNEMONIC_SCASD: case ZYDIS_MNEMONIC_SCASQ: do_scas(); break;
        default: return false;
        }
        return true;
    }

    // REP / REPE / REPNE loop. RCX = iteration count; loops until RCX=0
    // (for REP STOS/MOVS/LODS) or until RCX=0 or ZF mismatch (for
    // REPE/REPNE CMPS/SCAS).
    const bool is_cmps_or_scas = (mn == ZYDIS_MNEMONIC_CMPSB || mn == ZYDIS_MNEMONIC_CMPSW ||
                                  mn == ZYDIS_MNEMONIC_CMPSD || mn == ZYDIS_MNEMONIC_CMPSQ ||
                                  mn == ZYDIS_MNEMONIC_SCASB || mn == ZYDIS_MNEMONIC_SCASW ||
                                  mn == ZYDIS_MNEMONIC_SCASD || mn == ZYDIS_MNEMONIC_SCASQ);
    while (s.gpr[GPR_RCX] != 0) {
        switch (mn) {
        case ZYDIS_MNEMONIC_MOVSB: case ZYDIS_MNEMONIC_MOVSW:
        case ZYDIS_MNEMONIC_MOVSD: case ZYDIS_MNEMONIC_MOVSQ: do_movs(); break;
        case ZYDIS_MNEMONIC_STOSB: case ZYDIS_MNEMONIC_STOSW:
        case ZYDIS_MNEMONIC_STOSD: case ZYDIS_MNEMONIC_STOSQ: do_stos(); break;
        case ZYDIS_MNEMONIC_LODSB: case ZYDIS_MNEMONIC_LODSW:
        case ZYDIS_MNEMONIC_LODSD: case ZYDIS_MNEMONIC_LODSQ: do_lods(); break;
        case ZYDIS_MNEMONIC_CMPSB: case ZYDIS_MNEMONIC_CMPSW:
        case ZYDIS_MNEMONIC_CMPSD: case ZYDIS_MNEMONIC_CMPSQ: do_cmps(); break;
        case ZYDIS_MNEMONIC_SCASB: case ZYDIS_MNEMONIC_SCASW:
        case ZYDIS_MNEMONIC_SCASD: case ZYDIS_MNEMONIC_SCASQ: do_scas(); break;
        default: return false;
        }
        --s.gpr[GPR_RCX];
        if (is_cmps_or_scas) {
            const bool zf = GetFlag(s, RflagsBits::ZF);
            if (has_repe && !zf) break; // REPE: stop on first mismatch
            if (has_repne && zf) break; // REPNE: stop on first match
        }
        if (s.gpr[GPR_RCX] == 0) break;
    }
    return true;
}

} // namespace Core::Cpu

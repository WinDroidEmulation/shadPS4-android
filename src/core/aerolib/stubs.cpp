// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef ARCH_X86_64
#include <xbyak/xbyak.h>
#endif

#include "common/arch.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/aerolib/aerolib.h"
#include "core/aerolib/stubs.h"

#ifdef ARCH_ARM64
#include <sys/mman.h>
#endif

namespace Core::AeroLib {

// Helper to provide stub implementations for missing functions
//
// This works by constructing a minimal trampoline for each new stub which then jumps to a common
// handler with its index provided as a parameter so nid info can be found

#ifdef ARCH_X86_64

struct StubEntry {
    const NidEntry* nid = nullptr;
    std::string nid_unknown;
    std::unique_ptr<Xbyak::CodeGenerator> code;
};

static std::vector<StubEntry> g_stub_entries;
static std::mutex g_stub_mutex;

u64 PS4_SYSV_ABI CommonStub(u64 index) {

    const auto& e = g_stub_entries[index];
    if (e.nid) {
        LOG_ERROR(Core, "Stub: {} (nid: {}) called, returning zero to {}", e.nid->name, e.nid->nid,
                  __builtin_return_address(0));
    } else {
        LOG_ERROR(Core, "Stub: Unknown (nid: {}) called, returning zero to {}", e.nid_unknown,
                  __builtin_return_address(0));
    }
    return 0;
}

u64 GetStub(const char* nid) {
    std::scoped_lock lock{g_stub_mutex};

    if (g_stub_entries.empty()) {
        g_stub_entries.reserve(500);
    }

    const u64 index = g_stub_entries.size();

    StubEntry e;
    if (const auto* entry = FindByNid(nid)) {
        if (auto const& it = std::ranges::find_if(
                g_stub_entries, [entry](StubEntry const& en) { return en.nid && en.nid == entry; });
            it != g_stub_entries.end()) {
            return reinterpret_cast<u64>(it->code->getCode());
        }
        e.nid = entry;
    } else {
        e.nid_unknown = nid;
    }

    e.code = std::make_unique<Xbyak::CodeGenerator>(32, Xbyak::AutoGrow);
    e.code->mov(e.code->rdi, index);
    e.code->mov(e.code->rax, reinterpret_cast<u64>(&CommonStub));
    e.code->jmp(e.code->rax);
    e.code->ready();

    g_stub_entries.push_back(std::move(e));
    return reinterpret_cast<u64>(g_stub_entries.back().code->getCode());
}

#else

// AArch64: Xbyak cannot emit ARM64 code, so the trampolines are hand-encoded.
// Each trampoline performs the moral equivalent of the x86_64 version:
//   mov w0, index          (first argument, zero-extended to x0)
//   load x16, &CommonStub
//   br x16

struct StubEntry {
    const NidEntry* nid = nullptr;
    std::string nid_unknown;
    u8* code = nullptr;
};

static std::vector<StubEntry> g_stub_entries;
static std::mutex g_stub_mutex;

u64 PS4_SYSV_ABI CommonStub(u64 index) {

    const auto& e = g_stub_entries[index];
    if (e.nid) {
        LOG_ERROR(Core, "Stub: {} (nid: {}) called, returning zero to {}", e.nid->name, e.nid->nid,
                  __builtin_return_address(0));
    } else {
        LOG_ERROR(Core, "Stub: Unknown (nid: {}) called, returning zero to {}", e.nid_unknown,
                  __builtin_return_address(0));
    }
    return 0;
}

static constexpr std::size_t kTrampolineSize = 32; // 8 ARM64 instructions
static constexpr std::size_t kArenaSize = 1ull << 20;

static u8* g_arena = nullptr;
static std::size_t g_arena_used = 0;

static u8* AllocateTrampoline(u64 index, void* target) {
    if (g_arena == nullptr || g_arena_used + kTrampolineSize > kArenaSize) {
        void* p = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_MSG(p != MAP_FAILED, "Failed to allocate stub trampoline arena");
        g_arena = static_cast<u8*>(p);
        g_arena_used = 0;
        // Tell the x86-64 interpreter's HLE bridge where the stub arena
        // lives so it can detect calls into stubs and divert them to
        // AeroLibCommonStub instead of trying to execute the ARM64
        // trampoline code.
        Core_Cpu_RegisterAerolibStubArena(reinterpret_cast<u64>(g_arena), kArenaSize);
    }

    u8* code = g_arena + g_arena_used;
    g_arena_used += kTrampolineSize;

    u32* i = reinterpret_cast<u32*>(code);
    // movz w0, #(index & 0xFFFF)
    *i++ = 0x52800000u | static_cast<u32>((index & 0xFFFFu) << 5);
    // movk w0, #((index >> 16) & 0xFFFF), lsl #16
    *i++ = 0x72A00000u | static_cast<u32>(((index >> 16) & 0xFFFFu) << 5);
    const u64 addr = reinterpret_cast<u64>(target);
    // movz x16, #(addr & 0xFFFF)
    *i++ = 0xD2800000u | static_cast<u32>((addr & 0xFFFFu) << 5) | 16;
    // movk x16, #((addr >> shift) & 0xFFFF), lsl #shift for shift = 16/32/48
    *i++ = 0xF2800000u | static_cast<u32>(((addr >> 16) & 0xFFFFu) << 5) | (1u << 21) | 16;
    *i++ = 0xF2800000u | static_cast<u32>(((addr >> 32) & 0xFFFFu) << 5) | (2u << 21) | 16;
    *i++ = 0xF2800000u | static_cast<u32>(((addr >> 48) & 0xFFFFu) << 5) | (3u << 21) | 16;
    // br x16
    *i++ = 0xD61F0200u;

    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + kTrampolineSize));
    return code;
}

u64 GetStub(const char* nid) {
    std::scoped_lock lock{g_stub_mutex};

    if (g_stub_entries.empty()) {
        g_stub_entries.reserve(500);
    }

    const u64 index = g_stub_entries.size();

    StubEntry e;
    if (const auto* entry = FindByNid(nid)) {
        if (auto const& it = std::ranges::find_if(
                g_stub_entries, [entry](StubEntry const& en) { return en.nid && en.nid == entry; });
            it != g_stub_entries.end()) {
            return reinterpret_cast<u64>(it->code);
        }
        e.nid = entry;
    } else {
        e.nid_unknown = nid;
    }

    e.code = AllocateTrampoline(index, reinterpret_cast<void*>(&CommonStub));

    g_stub_entries.push_back(std::move(e));
    return reinterpret_cast<u64>(g_stub_entries.back().code);
}

#endif

} // namespace Core::AeroLib

// C-linkage wrapper for the x86-64 interpreter's HLE bridge (see
// src/core/cpu/interpreter/x64_hle_bridge.cpp). The interpreter can't
// execute the ARM64 trampoline code in the stub arena, so it calls
// this wrapper instead which forwards to the static CommonStub
// function inside the Core::AeroLib namespace.
extern "C" u64 PS4_SYSV_ABI AeroLibCommonStub(u64 index) {
    return Core::AeroLib::CommonStub(index);
}

// Called by the stub arena allocator on first allocation so the
// interpreter can detect stub addresses and divert them to the HLE
// bridge. We extern-declare this here (defined in
// src/core/cpu/interpreter/x64_hle_bridge.cpp) so the ARM64 branch of
// stubs.cpp can call it.
extern "C" void Core_Cpu_RegisterAerolibStubArena(u64 base, u64 size);

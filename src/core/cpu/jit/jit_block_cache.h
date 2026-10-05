// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Block cache for the ARM64 JIT. Maps guest RIP → translated ARM64 code.
// Uses a hash map for lookup and mmap'd executable memory for code storage.
//
// On a cache miss, the JIT translates the x86-64 basic block starting at
// the guest RIP into ARM64 code, stores it in the executable region, and
// records the entry point in the hash map. Subsequent hits jump directly
// to the translated code — no decode overhead.

#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "common/types.h"

namespace Core::Cpu::Jit {

// Maximum size of a single translated block (in bytes of ARM64 code).
// 4 KB is enough for ~1000 ARM64 instructions, which covers even the
// longest x86-64 basic blocks we're likely to see.
constexpr size_t kMaxBlockSize = 4096;

// Total executable code region size. 64 MB is enough for ~16K blocks.
// If we run out, the cache is cleared and blocks are retranslated on demand.
constexpr size_t kCodeRegionSize = 64 * 1024 * 1024;

// A translated block entry.
struct BlockEntry {
    void* host_code = nullptr;      // Entry point in executable memory
    u64   guest_rip = 0;            // Starting guest RIP
    u32   guest_size = 0;           // Bytes of guest code this block covers
    u32   host_size = 0;            // Bytes of ARM64 code emitted
    u64   hit_count = 0;            // How many times this block was executed
    bool  valid = false;            // Whether this entry is live
};

// Block cache: manages executable memory and the guest_rip → block lookup.
class BlockCache {
public:
    BlockCache();
    ~BlockCache();

    // Look up a translated block by guest RIP. Returns nullptr if not cached.
    const BlockEntry* Lookup(u64 guest_rip) const;

    // Allocate space for a new block of `size` bytes. Returns a pointer
    // to the start of the allocated region (writable, NOT yet executable).
    // The caller writes ARM64 code into this region, then calls FinalizeBlock.
    void* AllocBlock(u64 guest_rip, size_t size);

    // Finalize a block: mark it as valid, make it executable, and register
    // it in the lookup table. `host_ptr` must be a pointer returned by
    // AllocBlock. `guest_size` is how many guest bytes the block covers.
    void FinalizeBlock(u64 guest_rip, void* host_ptr, size_t host_size, u32 guest_size);

    // Invalidate a single block (e.g., on self-modifying code or page
    // protection change). Falls back to invalidating all blocks in the
    // same guest page.
    void Invalidate(u64 guest_rip);

    // Invalidate all blocks (full cache flush). Used when the executable
    // region is full.
    void InvalidateAll();

    // Stats
    size_t NumBlocks() const { return m_blocks.size(); }
    size_t CodeUsed() const { return m_code_offset; }
    size_t CodeCapacity() const { return kCodeRegionSize; }

private:
    void* m_code_region = nullptr;   // mmap'd RWX region
    size_t m_code_offset = 0;        // Next free offset in code region
    std::unordered_map<u64, BlockEntry> m_blocks;
    mutable std::mutex m_mutex;
};

} // namespace Core::Cpu::Jit

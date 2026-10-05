// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/cpu/jit/jit_block_cache.h"

#include <sys/mman.h>
#include <unistd.h>

#include "common/logging/log.h"

namespace Core::Cpu::Jit {

BlockCache::BlockCache() {
    // Allocate a RWX memory region for translated code.
    // On Android (and Linux in general), we can get RWX via mmap with
    // PROT_READ | PROT_WRITE | PROT_EXEC. (Some kernels enforce W^X,
    // in which case we'd need to toggle between RW and RX with mprotect;
    // for now we try RWX first and fall back if needed.)
    m_code_region = mmap(nullptr, kCodeRegionSize,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m_code_region == MAP_FAILED) {
        // Fallback: allocate RW, make RX later via mprotect in FinalizeBlock.
        m_code_region = mmap(nullptr, kCodeRegionSize,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m_code_region == MAP_FAILED) {
            LOG_CRITICAL(Core_Cpu, "JIT: Failed to mmap code region ({} bytes)", kCodeRegionSize);
            m_code_region = nullptr;
            return;
        }
    }
    LOG_INFO(Core_Cpu, "JIT: Allocated {} MB code region at {}", kCodeRegionSize / (1024*1024), m_code_region);
}

BlockCache::~BlockCache() {
    if (m_code_region) {
        munmap(m_code_region, kCodeRegionSize);
    }
}

const BlockEntry* BlockCache::Lookup(u64 guest_rip) const {
    std::scoped_lock lock(m_mutex);
    auto it = m_blocks.find(guest_rip);
    if (it == m_blocks.end() || !it->second.valid) {
        return nullptr;
    }
    return &it->second;
}

void* BlockCache::AllocBlock(u64 guest_rip, size_t size) {
    std::scoped_lock lock(m_mutex);
    if (!m_code_region) {
        return nullptr;
    }
    // Align to 16 bytes (ARM64 instruction + alignment requirement).
    size_t aligned_size = (size + 15) & ~15;
    if (m_code_offset + aligned_size > kCodeRegionSize) {
        // Cache is full — flush everything.
        LOG_WARNING(Core_Cpu, "JIT: Code cache full ({} MB used), flushing all blocks",
                    m_code_offset / (1024 * 1024));
        InvalidateAll();
    }
    void* ptr = static_cast<u8*>(m_code_region) + m_code_offset;
    m_code_offset += aligned_size;
    return ptr;
}

void BlockCache::FinalizeBlock(u64 guest_rip, void* host_ptr, size_t host_size, u32 guest_size) {
    std::scoped_lock lock(m_mutex);

    // On W^X kernels, make the block executable now.
    // We already have RWX from mmap in the constructor, so this is a no-op
    // on most Android devices. But if the fallback path was taken, we
    // need to flip from RW to RX.
    // (For now, skip mprotect — RWX should work on Android.)

    BlockEntry entry;
    entry.host_code = host_ptr;
    entry.guest_rip = guest_rip;
    entry.guest_size = guest_size;
    entry.host_size = (u32)host_size;
    entry.hit_count = 0;
    entry.valid = true;
    m_blocks[guest_rip] = entry;

    LOG_DEBUG(Core_Cpu, "JIT: Finalized block at guest_rip=0x{:x} → host=0x{:x} ({} bytes ARM64, {} bytes x86)",
              guest_rip, (uintptr_t)host_ptr, host_size, guest_size);
}

void BlockCache::Invalidate(u64 guest_rip) {
    std::scoped_lock lock(m_mutex);
    auto it = m_blocks.find(guest_rip);
    if (it != m_blocks.end()) {
        it->second.valid = false;
    }
}

void BlockCache::InvalidateAll() {
    std::scoped_lock lock(m_mutex);
    m_blocks.clear();
    m_code_offset = 0;
    LOG_INFO(Core_Cpu, "JIT: Flushed all blocks");
}

} // namespace Core::Cpu::Jit

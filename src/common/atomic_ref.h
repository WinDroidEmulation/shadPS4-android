// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <concepts>
#include <memory>
#include <type_traits>

namespace Common {

// Minimal drop-in replacement for std::atomic_ref, which is still missing
// from libc++ as shipped in the Android NDK (LLVM 19). Only covers the
// operations used by the emulator sources; the implementation overlays
// std::atomic operations on the referenced object, which is well-defined for
// trivially copyable types of matching size and alignment.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class atomic_ref {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Common::atomic_ref requires a trivially copyable type");
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8 ||
                      sizeof(T) == 16,
                  "Common::atomic_ref supports 1/2/4/8/16 byte types");

    T* ptr_;

public:
    using value_type = T;
    static constexpr bool is_always_lock_free =
        __atomic_always_lock_free(sizeof(T), nullptr);

    explicit atomic_ref(T& ref) : ptr_(std::addressof(ref)) {}
    atomic_ref(const atomic_ref&) noexcept = default;

    static constexpr std::size_t required_alignment = alignof(T);

    bool is_lock_free() const noexcept {
        return is_always_lock_free;
    }

    void store(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
        __atomic_store_n(ptr_, desired, static_cast<int>(order));
    }

    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(ptr_, static_cast<int>(order));
    }

    operator T() const noexcept {
        return load();
    }

    T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_exchange_n(ptr_, desired, static_cast<int>(order));
    }

    bool compare_exchange_weak(T& expected, T desired,
                               std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, true, static_cast<int>(order), static_cast<int>(order));
    }

    bool compare_exchange_strong(T& expected, T desired,
                                 std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, false, static_cast<int>(order), static_cast<int>(order));
    }

    T fetch_add(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept
        requires std::integral<T> || std::floating_point<T> || std::is_pointer_v<T>
    {
        return __atomic_fetch_add(ptr_, value, static_cast<int>(order));
    }

    T fetch_sub(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept
        requires std::integral<T> || std::floating_point<T> || std::is_pointer_v<T>
    {
        return __atomic_fetch_sub(ptr_, value, static_cast<int>(order));
    }

    T fetch_and(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept
        requires std::integral<T>
    {
        return __atomic_fetch_and(ptr_, value, static_cast<int>(order));
    }

    T fetch_or(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept
        requires std::integral<T>
    {
        return __atomic_fetch_or(ptr_, value, static_cast<int>(order));
    }

    T fetch_xor(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept
        requires std::integral<T>
    {
        return __atomic_fetch_xor(ptr_, value, static_cast<int>(order));
    }

    T operator++(int) const noexcept
        requires std::integral<T>
    {
        return fetch_add(1);
    }

    T operator--(int) const noexcept
        requires std::integral<T>
    {
        return fetch_sub(1);
    }

    T operator=(T desired) const noexcept {
        store(desired);
        return desired;
    }
};

} // namespace Common

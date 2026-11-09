#pragma once
// A spinlock and its guard. A std::mutex unlock wakes blocked waiters through a futex syscall,
// so the engine thread's code never references one: it only ever try_lock()s a SpinMutex and
// skips. Critical sections must stay tiny — a waiter burns its core while it waits.

#include <atomic>

namespace tetrad {

class SpinMutex {
public:
    void lock() noexcept
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {}
    }
    bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

class SpinGuard {
public:
    explicit SpinGuard(SpinMutex& m) noexcept : m_(m) { m_.lock(); }
    ~SpinGuard() { m_.unlock(); }
    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;

private:
    SpinMutex& m_;
};

} // namespace tetrad

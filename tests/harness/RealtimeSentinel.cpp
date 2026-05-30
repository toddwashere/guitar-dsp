#include "RealtimeSentinel.h"

#include <atomic>
#include <new>

namespace guitar_dsp::tests {

namespace {
    thread_local bool g_isRealtime = false;
    std::atomic<std::size_t> g_violationCount{0};
}

RealtimeSentinel::RealtimeSentinel()
    : baseline_(g_violationCount.load(std::memory_order_relaxed)) {}

RealtimeSentinel::~RealtimeSentinel() {
    // Defensive: ensure we leave no dangling realtime marker.
    g_isRealtime = false;
}

void RealtimeSentinel::markCurrentThreadAsRealtime() { g_isRealtime = true; }
void RealtimeSentinel::unmarkCurrentThreadAsRealtime() { g_isRealtime = false; }

std::size_t RealtimeSentinel::violations() const {
    return g_violationCount.load(std::memory_order_relaxed) - baseline_;
}

bool RealtimeSentinel::isCurrentThreadRealtime() { return g_isRealtime; }
void RealtimeSentinel::recordViolation() {
    g_violationCount.fetch_add(1, std::memory_order_relaxed);
}

} // namespace guitar_dsp::tests

// Global operator new/delete overrides — applied to the test binary only.
// We can't throw from `operator delete`, so we just record and return.

void* operator new(std::size_t size) {
    if (guitar_dsp::tests::RealtimeSentinel::isCurrentThreadRealtime()) {
        guitar_dsp::tests::RealtimeSentinel::recordViolation();
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* p) noexcept {
    if (guitar_dsp::tests::RealtimeSentinel::isCurrentThreadRealtime()) {
        guitar_dsp::tests::RealtimeSentinel::recordViolation();
    }
    std::free(p);
}

void operator delete[](void* p) noexcept {
    ::operator delete(p);
}

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }

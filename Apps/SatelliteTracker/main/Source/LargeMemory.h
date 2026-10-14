#pragma once

#include <cstddef>

/** Allocates big buffers in external RAM (PSRAM) when the device has it. Returns nullptr when out of memory. */
void* largeAlloc(size_t size);
/** Like largeAlloc(), but stops the app with an error message when out of memory, for callers that cannot handle nullptr. */
void* largeAllocOrCrash(size_t size);
void largeFree(void* pointer);

/** std::vector allocator that keeps the elements in external RAM when available. */
template <typename T>
struct LargeAllocator {
    using value_type = T;

    LargeAllocator() = default;

    template <typename U>
    LargeAllocator(const LargeAllocator<U>&) {}

    /** std::vector cannot handle nullptr and exceptions are disabled, so running out of memory stops the app here. */
    T* allocate(size_t count) { return static_cast<T*>(largeAllocOrCrash(count * sizeof(T))); }
    void deallocate(T* pointer, size_t) { largeFree(pointer); }

    template <typename U>
    bool operator==(const LargeAllocator<U>&) const { return true; }
    template <typename U>
    bool operator!=(const LargeAllocator<U>&) const { return false; }
};

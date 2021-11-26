#pragma once
//---------------------------------------------------------------------------
#include <cstdlib>
#include <cassert>
#include <unordered_set>
//---------------------------------------------------------------------------
template <typename T>
class MallocAllocator {
    public:
    /// Default constructor
    MallocAllocator() = default;

    /// MallocAllocator can't be copied
    MallocAllocator(const MallocAllocator&) = delete;

    /// MallocAllocator can't be copied
    MallocAllocator& operator=(const MallocAllocator&) = delete;

    /// Move constructor
    MallocAllocator(MallocAllocator&&) noexcept = default;

    /// Move assignment
    MallocAllocator& operator=(MallocAllocator&&) noexcept = default;

    /// Allocate
    T* allocate() { return reinterpret_cast<T*>(std::malloc(sizeof(T))); }

    /// Deallocate
    void deallocate(T* ptr) { std::free(reinterpret_cast<void*>(ptr)); }

    /// A nested templated type alias for rebinding
    /// Its member type other is the equivalent allocator type to allocate elements of type T
    template<typename U>
    using rebind = MallocAllocator<U>;
};
//---------------------------------------------------------------------------

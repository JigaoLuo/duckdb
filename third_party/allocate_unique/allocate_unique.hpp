// From: https://gist.github.com/atosh/427a2ed16cef5ff582bdb17480aee009
// Released under the CC0 1.0 Universal license.
// Requires C++14 compiler.
#include <memory>

namespace std {

template <typename Allocator>
struct Deallocator {
    using value_type = typename Allocator::value_type;
    Allocator alloc_;
    size_t size_;
    explicit Deallocator(const Allocator& alloc, size_t size) : alloc_(alloc), size_(size) {}
    void operator()(value_type* ptr)
    {
        using Traits = std::allocator_traits<Allocator>;
        for (ptrdiff_t i = size_ - 1; i >= 0; --i) {
            Traits::destroy(alloc_, ptr + i);
        }
        Traits::deallocate(alloc_, ptr, size_);
    }
};

template <typename Allocator, typename U>
using Rebind = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;

template <typename T, typename Allocator, typename... Args>
inline std::enable_if_t<!is_array<T>::value, std::unique_ptr<T, Deallocator<Rebind<Allocator, T>>>>
allocate_unique(const Allocator& allocator, Args&&... args) {
    using Alloc = Rebind<Allocator, T>;
    using Traits = std::allocator_traits<Alloc>;
    Alloc alloc(allocator);
    T* ptr = Traits::allocate(alloc, 1);
    try {
        Traits::construct(alloc, ptr, std::forward<Args>(args)...);
    } catch (...) {
        Traits::deallocate(alloc, ptr, 1);
    }
    Deallocator<Alloc> dealloc(alloc, 1);
    return std::unique_ptr<T, Deallocator<Alloc>>(ptr, dealloc);
}

template <typename T, typename Allocator>
inline std::enable_if_t<is_array<T>::value && extent<T>::value == 0, std::unique_ptr<T, Deallocator<Rebind<Allocator, remove_extent_t<T>>>>>
allocate_unique(const Allocator& allocator, size_t size) {
    using Elem = remove_extent_t<T>;
    using Alloc = Rebind<Allocator, Elem>;
    using Traits = std::allocator_traits<Alloc>;
    Alloc alloc(allocator);
    Elem* ptr = Traits::allocate(alloc, size);
    size_t i = 0;
    try {
        for (; i < size; ++i) {
            Traits::construct(alloc, ptr + i);
        }
    } catch (...) {
        for (size_t j = 0; j < i; ++j) {
            Traits::destroy(alloc, ptr + j);
        }
        Traits::deallocate(alloc, ptr, size);
    }
    Deallocator<Alloc> dealloc(alloc, size);
    return std::unique_ptr<T, Deallocator<Alloc>>(ptr, dealloc);
}

}  // namespace std
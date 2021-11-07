// From: https://gist.github.com/atosh/427a2ed16cef5ff582bdb17480aee009
//https://stackoverflow.com/questions/20435341/what-is-the-best-unique-pointer-for-use-with-a-custom-allocator
// Released under the CC0 1.0 Universal license.
// Requires C++14 compiler.
#include <memory>
#include <new>
#include <cstdlib>

template<typename T, typename... Args>
T* allocate(Args&&... args) {
    std::unique_ptr<T, void(*)(void*)> hold(static_cast<T*>(std::malloc(sizeof(T))), std::free);
    ::new (hold.get()) T(std::forward<Args>(args)...);
    return static_cast<T*>(hold.release());
}

template<typename T>
void deallocate(void* p) {
    static_cast<T*>(p)->~T();
    std::free(p);
}






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
inline std::enable_if_t<!std::is_array<T>::value, std::unique_ptr<T, Deallocator<Rebind<Allocator, T>>>>
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
inline std::enable_if_t<std::is_array<T>::value && std::extent<T>::value == 0, std::unique_ptr<T, Deallocator<Rebind<Allocator, std::remove_extent_t<T>>>>>
allocate_unique(const Allocator& allocator, size_t size) {
    using Elem = std::remove_extent_t<T>;
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

//unique_ptr<Leaf, std::function<void(Leaf*)>> nodeee = allocate_unique<Leaf>(allocator, *this, move(value), row_id);
//unique_ptr<Node, std::function<void(Leaf*)>> nodeee2 = allocate_unique<Leaf>(allocator, *this, move(value), row_id);
//unique_ptr<Leaf, Deallocator<std::allocator<Leaf>>> nodeee3 = allocate_unique<Leaf>(allocator, *this, move(value), row_id);
//unique_ptr<Leaf, Deallocator<Rebind<Allocator, Leaf>>> nodeee4 = allocate_unique<Leaf>(allocator, *this, move(value), row_id);
//unique_ptr<Node, Deallocator<std::allocator<Leaf>>> nodeee5 = allocate_unique<Leaf>(allocator, *this, move(value), row_id);
//unique_ptr<Node, Deallocator<Rebind<Allocator, Leaf>>> nodeee6 = allocate_unique<Leaf>(allocator, *this, move(value), row_id);



//template <class T, class A, class ...Args>
//auto allocator_new(A& alloc, Args&&... args) {
//    using TTraits = typename std::allocator_traits<A>::template rebind_traits<T>;
//    using TAlloc = typename std::allocator_traits<A>::template rebind_alloc<T>;
//
//    auto a = TAlloc(alloc);
//    auto p = TTraits::allocate(a, 1);
//
//    try {
//        TTraits::construct(a, std::to_address(p), std::forward<Args>(args)...);
//        return p;
//    } catch(...) {
//        TTraits::deallocate(a, p, 1);
//        throw;
//    }
//}
//
//template <class A, class P>
//void allocator_delete(A& alloc, P p) {
//    using Elem = typename std::pointer_traits<P>::element_type;
//    using Traits = typename std::allocator_traits<A>::template rebind_traits<Elem>;
//
//    Traits::destroy(alloc, std::to_address(p));
//    Traits::deallocate(alloc, p, 1);
//}
//
//template <class A>
//struct allocation_deleter {
//    using pointer = typename std::allocator_traits<A>::pointer;
//
//    A a_;  // exposition only
//
//    allocation_deleter(const A& a) noexcept : a_(a) {}
//
//    void operator()(pointer p) {
//        allocator_delete(a_, p);
//    }
//};
//
//template <class T, class A, class ...Args>
//auto allocate_unique(A& alloc, Args&&... args) {
//    using TAlloc = typename std::allocator_traits<A>::template rebind_alloc<T>;
//    return std::unique_ptr<T, allocation_deleter<TAlloc>>(allocator_new<T>(alloc, std::forward<Args>(args)...), alloc);
//}
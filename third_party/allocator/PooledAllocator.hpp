#pragma once
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <cassert>
//---------------------------------------------------------------------------
template <typename T>
class PooledAllocator {
    private:
    /// Chunk for memory allocation
    struct Chunk {
        /// Pointer to next chunk.
        Chunk* next_chunk = nullptr;
        /// First free slot for type T
        T* first_free = nullptr;
    };

    /// A singly linked-list for Chunk
    struct SLList {
        /// Header pointer of the singly linked-list
        Chunk* head = nullptr;
        /// Tail pointer of the singly linked-list (the last element, not the past-the-end flag)
        Chunk* tail = nullptr;

        /// Default Constructor
        SLList() = default;

        /// Destructor
        ~SLList() {
            while (head != nullptr) {
                Chunk* temp = head;
                head = head->next_chunk;
                free(temp);
            }
        }

        /// SLList can't be copied
        SLList(const SLList& other) = delete;

        /// SLList can't be copied
        SLList& operator=(const SLList&) = delete;

        /// Move constructor
        SLList(SLList&& other) noexcept : head(other.head), tail(other.tail) {
            other.head = nullptr;
            other.tail = nullptr;
        };

        /// Move assignment
        SLList& operator=(SLList&& other) noexcept {
            if (this == &other) {
                return *this;
            }
            head = other.head;
            tail = other.tail;
            other.head = nullptr;
            other.tail = nullptr;
            return *this;
        };
    };

    /// Simply linked list for chunks
    SLList l;

    /// Chunk size starts with 4096 * 4 bytes. Only record the current (tail) chunk.
    size_t tail_chunk_size = 4096 * 4;

    /// Record the remaning size of the last chunk. Only record the current (tail) chunk.
    size_t tail_chunk_remaining_size = 0;

    public:
    /// Default constructor
    PooledAllocator() = default;

    /// Default destructor
    ~PooledAllocator() = default;

    /// PooledAllocator can't be copied
    PooledAllocator(const PooledAllocator&) = delete;

    /// PooledAllocator can't be copied
    PooledAllocator& operator=(const PooledAllocator&) = delete;

    /// Move constructor
    PooledAllocator(PooledAllocator&&) noexcept = default;

    /// Move assignment
    PooledAllocator& operator=(PooledAllocator&&) noexcept = default;

    /// Allocate
    T* allocate() {
        if (l.head == nullptr && l.tail == nullptr) {
            // Init the list -- first allocation
            l.head = reinterpret_cast<Chunk*>(std::malloc(tail_chunk_size));
            l.head->next_chunk = nullptr;
            l.head->first_free = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(l.head) + sizeof(Chunk));
            l.tail = l.head;
            tail_chunk_remaining_size = tail_chunk_size - sizeof(Chunk);
        } else if (tail_chunk_remaining_size < sizeof(T)) {
            // Current chunk has no enough space for an allocation. So to allocate a new larger chunk and link it at the end of the singly linked list.
            tail_chunk_size *= 2;
            auto new_memory = reinterpret_cast<Chunk*>(std::malloc(tail_chunk_size));
            l.tail->next_chunk = new_memory;
            l.tail = new_memory;
            l.tail->next_chunk = nullptr;
            l.tail->first_free = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(new_memory) + sizeof(Chunk));
            tail_chunk_remaining_size = tail_chunk_size - sizeof(Chunk);
        }
        tail_chunk_remaining_size -= sizeof(T);
        return l.tail->first_free++;
    }

    /// Deallocate
    void deallocate(T* ptr) {
        if (ptr == l.tail->first_free - 1) {
            // If at the end of the last chunk, then re-use.
            l.tail->first_free--;
        }
    }

    /// A nested templated type alias for rebinding
    /// Its member type other is the equivalent allocator type to allocate elements of type T
    template<typename U>
    using rebind = PooledAllocator<U>;
};
//---------------------------------------------------------------------------

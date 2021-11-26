#pragma once

#include <cstdint>
#include <vector>
#include <iostream>

#include "mmap_allocator.hpp"
#include "../ART/ART_nodes.hpp"

#define DEBUG 1

template <page_type PageType, unsigned NumaNode>
struct art_mmap_allocator {
	size_t num_free_bytes = 0;
    uint8_t* memory = nullptr;
    mmap_allocator<uint8_t, PageType, NumaNode> allocator;
    std::vector<uint8_t*> allocated_pages;

    constexpr static std::size_t SIZE_2MB = 2ull * 1024ull * 1024ull;
    constexpr static std::size_t SIZE_16MB = 16ull * 1024ull * 1024ull;
    constexpr static std::size_t SIZE_1GB = 1024ull * 1024ull * 1024ull;
    constexpr static std::size_t SIZE_16GB = 16ull * 1024ull * 1024ull * 1024ull;

	art_mmap_allocator() {
        mmap_allocator<uint8_t, PageType, NumaNode> allocator;
        switch (PageType) {
		    case huge_2mb: num_free_bytes = SIZE_2MB; break;
            case huge_16mb: num_free_bytes = SIZE_16MB; break;
            case huge_1gb: num_free_bytes = SIZE_1GB; break;
            case huge_16gb: num_free_bytes = SIZE_16GB; break;
		}
        memory = allocator.allocate(num_free_bytes);  /// here num_free_bytes as page size
        allocated_pages.push_back(memory);
#ifdef DEBUG
        std::cout << "Page Size: " << num_free_bytes << std::endl;
#endif
	}

    ~art_mmap_allocator() {
	    // Free all allocated pages.
        size_t page_size = 0;
        switch (PageType) {
            case huge_2mb: page_size = SIZE_2MB; break;
            case huge_16mb: page_size = SIZE_16MB; break;
            case huge_1gb: page_size = SIZE_1GB; break;
            case huge_16gb: page_size = SIZE_16GB; break;
        }
#ifdef DEBUG
        std::cout << "Deallocate number of pages: " << allocated_pages.size() << std::endl;
#endif
        for (auto& page : allocated_pages) {
            allocator.deallocate(page, page_size);
        }
	}

    art_mmap_allocator(const art_mmap_allocator& other) = delete;

 private:
	void allocate_new_page() {
        switch (PageType) {
            case huge_2mb: num_free_bytes = SIZE_2MB; break;
            case huge_16mb: num_free_bytes = SIZE_16MB; break;
            case huge_1gb: num_free_bytes = SIZE_1GB; break;
            case huge_16gb: num_free_bytes = SIZE_16GB; break;
        }
        memory = allocator.allocate(num_free_bytes);  /// here num_free_bytes as page size
        allocated_pages.push_back(memory);
#ifdef DEBUG
        std::cout << "Allocate a new page. " << std::endl;
#endif
	}

 public:
    uint8_t* allocate_node4() {
		if (num_free_bytes < sizeof(Node4)) {
            allocate_new_page();
		}
        uint8_t* result = memory;
		memory += sizeof(Node4);
        num_free_bytes -= sizeof(Node4);
#ifdef DEBUG
        std::cout << "On page " << allocated_pages.size() - 1 << " allocate a new NODE4.   Remaining bytes on current page: " << num_free_bytes << std::endl;
#endif
		return result;
	}

    uint8_t* allocate_node16() {
        if (num_free_bytes < sizeof(Node16)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node16);
        num_free_bytes -= sizeof(Node16);
#ifdef DEBUG
        std::cout << "On page " << allocated_pages.size() - 1 << " allocate a new NODE16.  Remaining bytes on current page: " << num_free_bytes << std::endl;
#endif
        return result;
    }

    uint8_t* allocate_node48() {
        if (num_free_bytes < sizeof(Node48)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node48);
        num_free_bytes -= sizeof(Node48);
#ifdef DEBUG
        std::cout << "On page " << allocated_pages.size() - 1 << " allocate a new NODE48.  Remaining bytes on current page: " << num_free_bytes << std::endl;
#endif
        return result;
    }

    uint8_t* allocate_node256() {
        if (num_free_bytes < sizeof(Node256)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node256);
        num_free_bytes -= sizeof(Node256);
#ifdef DEBUG
        std::cout << "On page " << allocated_pages.size() - 1 << " allocate a new NODE256. Remaining bytes on current page: " << num_free_bytes << std::endl;
#endif
        return result;
    }

	// TODO(jigao): how to do the node-level memory deallocation? if a node is deallocated, which is previously allocated in the middle of a page, this would result in a segmentation inside the page. Such segmentation is hard to track. => Is a slotted page an overkill?
	// TODO(jigao): Do I need to re-use the freed slot in the future allocation?
	// TODO(jigao): Do I free an entire page, when all nodes on it are deallocated?
};



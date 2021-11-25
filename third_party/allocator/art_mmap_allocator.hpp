#pragma once

#include "mmap_allocator.hpp"
#include "../ART/ART_nodes.hpp"

template <page_type PageType, unsigned NumaNode>
struct art_mmap_allocator {
	size_t num_free_bytes = 0;
    uint8_t* memory = nullptr;
    mmap_allocator<uint8_t, PageType, NumaNode> allocator;

    constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    constexpr size_t SIZE_1GB = 1024 * 1024 * 1024;
    constexpr size_t SIZE_16GB = 16 * 1024 * 1024 * 1024;

	art_mmap_allocator() {
        mmap_allocator<uint8_t, PageType, NumaNode> allocator;
        switch (PageType) {
		    case huge_2mb: num_free_bytes = SIZE_2MB; break;
            case huge_16mb: num_free_bytes = SIZE_16MB; break;
            case huge_1gb: num_free_bytes = SIZE_1GB; break;
            case huge_16gb: num_free_bytes = SIZE_16GB; break;
		}
        memory = allocator.allocate(num_free_bytes);
	}

    ~art_mmap_allocator() {}

    art_mmap_allocator(const art_mmap_allocator& other) = delete;

	allocate_new_page() {
        switch (PageType) {
            case huge_2mb: num_free_bytes = SIZE_2MB; break;
            case huge_16mb: num_free_bytes = SIZE_16MB; break;
            case huge_1gb: num_free_bytes = SIZE_1GB; break;
            case huge_16gb: num_free_bytes = SIZE_16GB; break;
        }
        memory = allocator.allocate(num_free_bytes);
	}

    uint8_t* allocate_node4() {
		if (num_free_bytes < sizeof(Node4)) {
            allocate_new_page();
		}
        uint8_t* result = memory;
		memory += sizeof(Node4);
		return result;
	}

    uint8_t* allocate_node16() {
        if (num_free_bytes < sizeof(Node16)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node16);
        return result;
    }

    uint8_t* allocate_node48() {
        if (num_free_bytes < sizeof(Node48)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node48);
        return result;
    }

    uint8_t* allocate_node256() {
        if (num_free_bytes < sizeof(Node256)) {
            allocate_new_page();
        }
        uint8_t* result = memory;
        memory += sizeof(Node256);
        return result;
    }
};



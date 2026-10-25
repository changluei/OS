// lab2 2412819: independent black-box Buddy System self-check.
#include <buddy_pmm.h>
#include <pmm.h>
#include <stdio.h>

/* The framework caps managed RAM at PHYSICAL_MEMORY_END, even with -m 256M.
 * Keep one byte per possible descriptor in BSS, not on the 8 KiB kernel stack.
 * This is test ownership, never allocator metadata: 0 = available, 1 = held by
 * this test, 2 = reserved at entry. No property/order/list field is inspected.
 */
#define CHECK_MAX_PAGES ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
static unsigned char check_owner[CHECK_MAX_PAGES];
static size_t check_descriptors;

static size_t
check_capacity(size_t request) {
    assert(request > 0 && request <= CHECK_MAX_PAGES);
    size_t capacity = 1;
    while (capacity < request) {
        capacity <<= 1;
    }
    return capacity;
}

/* Validate an untrusted returned descriptor numerically before subtracting or
 * dereferencing it. Validate the ENTIRE rounded allocation, not just its head.
 */
static size_t
check_index(struct Page *page, size_t capacity) {
    uintptr_t address = (uintptr_t)page;
    uintptr_t first = (uintptr_t)pages;
    assert(address >= first);
    assert((address - first) % sizeof(struct Page) == 0);
    size_t index = (address - first) / sizeof(struct Page);
    assert(index < check_descriptors);
    assert(capacity <= check_descriptors - index);
    assert((page2ppn(page) & (capacity - 1)) == 0);
    return index;
}

/* Touch only allocated physical memory, through the kernel's existing direct
 * mapping. Each page has three distinct address-dependent 64-bit canaries,
 * covering its start, interior and end (including rounding slack pages).
 */
static uint64_t
check_pattern(size_t index, size_t slot) {
    return 0x2412819BADD1E000ULL ^
           ((uint64_t)(nbase + index) * 0x9E3779B97F4A7C15ULL) ^
           ((uint64_t)(slot + 1) * 0xD1B54A32D192ED03ULL);
}

static void
check_page_contents(size_t index, bool write) {
    volatile uint64_t *memory = (volatile uint64_t *)
        (page2pa(pages + index) + va_pa_offset);
    const size_t offsets[] = {0, PGSIZE / sizeof(uint64_t) / 2,
                              PGSIZE / sizeof(uint64_t) - 1};
    for (size_t slot = 0; slot < 3; slot++) {
        uint64_t expected = check_pattern(index, slot);
        if (write) {
            memory[offsets[slot]] = expected;
        } else {
            assert(memory[offsets[slot]] == expected);
        }
    }
}

static void
check_live_contents(void) {
    for (size_t index = 0; index < check_descriptors; index++) {
        if (check_owner[index] == 1) {
            check_page_contents(index, 0);
        }
    }
}

static struct Page *
check_alloc(size_t request) {
    size_t before = nr_free_pages();
    struct Page *page = alloc_pages(request);
    if (page == NULL) {
        assert(nr_free_pages() == before);
        return NULL;
    }
    size_t capacity = check_capacity(request);
    assert(before >= capacity);
    assert(nr_free_pages() == before - capacity);
    size_t index = check_index(page, capacity);
    for (size_t offset = 0; offset < capacity; offset++) {
        assert(check_owner[index + offset] == 0);
        assert(!PageReserved(page + offset));
    }
    /* Validate overlap/reservation before writing any byte of this block. */
    for (size_t offset = 0; offset < capacity; offset++) {
        check_owner[index + offset] = 1;
        check_page_contents(index + offset, 1);
    }
    return page;
}

static void
check_free(struct Page *page, size_t original_request) {
    size_t capacity = check_capacity(original_request);
    size_t index = check_index(page, capacity);
    for (size_t offset = 0; offset < capacity; offset++) {
        assert(check_owner[index + offset] == 1);
        check_page_contents(index + offset, 0);
    }
    size_t before = nr_free_pages();
    free_pages(page, original_request);
    assert(nr_free_pages() == before + capacity);
    for (size_t offset = 0; offset < capacity; offset++) {
        check_owner[index + offset] = 0;
    }
}

static void
check_failure(size_t request) {
    assert(check_alloc(request) == NULL);
    check_live_contents();
}

static void
check_restored(size_t initial) {
    assert(nr_free_pages() == initial);
    for (size_t index = 0; index < check_descriptors; index++) {
        assert(check_owner[index] != 1);
        assert(!!PageReserved(pages + index) == (check_owner[index] == 2));
    }
}

static void
check_mixed_requests(size_t initial) {
    static const size_t requests[] = {1, 2, 3, 4, 5, 8, 9};
    static const size_t release_order[] = {3, 0, 5, 1, 6, 2, 4};
    struct Page *blocks[7];
    for (size_t slot = 0; slot < 7; slot++) {
        blocks[slot] = check_alloc(requests[slot]);
        assert(blocks[slot] != NULL);
        check_live_contents();
        /* Failures must preserve both the count and all live contents. */
        check_failure(0);
        check_failure((size_t)-1);
        check_failure(initial + 1);
    }
    struct Page *temporary = check_alloc(9);
    assert(temporary != NULL);
    check_live_contents();
    check_free(temporary, 9);
    check_live_contents();
    for (size_t slot = 0; slot < 7; slot++) {
        size_t victim = release_order[slot];
        check_free(blocks[victim], requests[victim]);
        check_live_contents();
    }
    check_restored(initial);
}

/* Every live allocation in this phase is a singleton, so ownership indexed by
 * descriptor is also a bounded, exact allocation record. Never infer adjacency
 * from allocation order or manipulate free lists to simulate exhaustion.
 */
static void
check_exhaust(void) {
    size_t remaining = nr_free_pages();
    while (remaining != 0) {
        assert(check_alloc(1) != NULL);
        remaining--;
        assert(nr_free_pages() == remaining);
    }
    check_failure(1);
    check_failure(2);
    check_failure(3);
}

static struct Page *
check_find_aligned_run(size_t capacity) {
    for (size_t index = 0; index < check_descriptors; index++) {
        if (((nbase + index) & (capacity - 1)) != 0 ||
            capacity > check_descriptors - index) {
            continue;
        }
        size_t offset = 0;
        while (offset < capacity && check_owner[index + offset] == 1) {
            offset++;
        }
        if (offset == capacity) {
            return pages + index;
        }
    }
    return NULL;
}

static void
check_fragmentation(void) {
    size_t released = 0;
    /* Free only even physical page numbers. No two free pages are buddies,
     * although total free capacity is ample for many larger requests.
     */
    for (size_t index = 0; index < check_descriptors; index++) {
        if (check_owner[index] == 1 && ((nbase + index) & 1) == 0) {
            check_free(pages + index, 1);
            released++;
        }
    }
    assert(released >= 16 && nr_free_pages() == released);
    check_failure(2);
    check_failure(3);
    check_failure(9);
    check_exhaust();

    /* The middle pages of a physically aligned four-page span are adjacent
     * but NOT buddies. An adjacency-only coalescer must fail this assertion.
     */
    struct Page *span = check_find_aligned_run(4);
    assert(span != NULL);
    check_free(span + 1, 1);
    check_free(span + 2, 1);
    assert(nr_free_pages() == 2);
    check_failure(2);
    check_exhaust();
}

static void
check_merge_and_split(void) {
    static const size_t release_four[] = {7, 5, 6, 4};
    static const size_t release_eight[] = {15, 9, 13, 11, 8, 10, 12, 14};
    static const size_t release_split[] = {1, 3, 0, 2};
    struct Page *span = check_find_aligned_run(16);
    struct Page *split[4];
    assert(span != NULL);

    check_free(span, 1);
    check_failure(2);                 // The other buddy is still live.
    check_free(span + 1, 1);
    struct Page *block = check_alloc(2);
    assert(block == span);            // These are the only two free pages.
    check_free(block, 2);
    check_failure(4);
    check_free(span + 3, 1);
    check_failure(4);                 // Three free pages cannot form four.
    check_free(span + 2, 1);
    block = check_alloc(3);           // Coalesce to four, free by request 3.
    assert(block == span);
    check_free(block, 3);

    for (size_t slot = 0; slot < 4; slot++) {
        check_free(span + release_four[slot], 1);
    }
    block = check_alloc(5);           // Recursive coalescing to eight.
    assert(block == span);
    check_free(block, 5);
    for (size_t slot = 0; slot < 8; slot++) {
        check_free(span + release_eight[slot], 1);
    }
    block = check_alloc(9);           // Recursive coalescing to sixteen.
    assert(block == span);
    check_live_contents();
    check_free(block, 9);

    for (size_t slot = 0; slot < 4; slot++) {
        split[slot] = check_alloc(3); // Split the recovered sixteen again.
        assert(split[slot] != NULL);
        assert(page2ppn(split[slot]) >= page2ppn(span));
        assert(page2ppn(split[slot]) + 4 <= page2ppn(span) + 16);
        check_live_contents();
    }
    assert(nr_free_pages() == 0);
    check_failure(1);
    for (size_t slot = 0; slot < 4; slot++) {
        check_free(split[release_split[slot]], 3);
        check_live_contents();
    }
    block = check_alloc(16);
    assert(block == span);
    check_free(block, 16);
    check_exhaust();                  // Restore singleton allocation records.
}

void
buddy_check(void) {
    assert(pmm_manager == &buddy_pmm_manager);
    assert(npage >= nbase);
    check_descriptors = npage - nbase;
    assert(check_descriptors <= CHECK_MAX_PAGES);
    size_t initial = nr_free_pages();
    assert(initial >= 64);            // Basic mixed-request peak is 59 pages.
    assert(initial <= check_descriptors);
    size_t available = 0;
    for (size_t index = 0; index < check_descriptors; index++) {
        check_owner[index] = PageReserved(pages + index) ? 2 : 0;
        available += check_owner[index] == 0;
    }
    /* page_init has finished and there are no other live allocations. Every
     * nonreserved descriptor must therefore be represented in the free count.
     * This also catches dropped tails in a non-power-of-two initial region.
     */
    assert(initial == available);

    /* Run the complete suite twice. The second pass detects stale allocation
     * metadata/list corruption that a final count-only check would overlook.
     */
    for (size_t pass = 0; pass < 2; pass++) {
        cprintf("buddy_check: pass %lu, initial free pages %lu\n", pass + 1,
                initial);
        check_failure(0);
        check_failure((size_t)-1);
        check_failure(initial + 1);
        check_mixed_requests(initial);
        check_exhaust();
        check_fragmentation();
        check_merge_and_split();
        /* Nonuniform final release: forward odd indices, reverse even ones. */
        for (size_t index = 1; index < check_descriptors; index += 2) {
            if (check_owner[index] == 1) {
                check_free(pages + index, 1);
            }
        }
        for (size_t cursor = check_descriptors; cursor != 0; cursor--) {
            size_t index = cursor - 1;
            if ((index & 1) == 0 && check_owner[index] == 1) {
                check_free(pages + index, 1);
            }
        }
        check_restored(initial);
    }
    cprintf("buddy_check() succeeded!\n");
}

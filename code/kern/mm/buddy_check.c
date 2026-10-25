/* lab2, student ID: 2412819
 * Black-box Buddy checks: only the public PMM contract is observed.
 */
#include <assert.h>
#include <buddy_pmm.h>
#include <pmm.h>
#include <stdio.h>

/* Keep the per-page records out of the 8 KiB kernel stack.  Descriptors cover
 * all DRAM, including reserved pages below the kernel mapping window;
 * unsupported layouts fail rather than truncate.
 */
#define BUDDY_CHECK_MAX_PAGES ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define BUDDY_CHECK_RESERVED 1U
#define BUDDY_CHECK_LIVE 2U

static uint8_t buddy_check_state[BUDDY_CHECK_MAX_PAGES];
static size_t buddy_check_page_count;
static size_t buddy_check_initial_free;
static uintptr_t buddy_check_seed;

static size_t
buddy_check_capacity(size_t request) {
    size_t capacity = 1;
    assert(request > 0 && request <= buddy_check_page_count);
    while (capacity < request) {
        assert(capacity <= (size_t)-1 / 2);
        capacity *= 2;
    }
    return capacity;
}

static size_t
buddy_check_index(struct Page *page, size_t capacity) {
    uintptr_t address = (uintptr_t)page;
    uintptr_t first = (uintptr_t)pages;
    size_t index;

    /* Validate the descriptor before subtraction or dereferencing it. */
    assert(address >= first);
    assert((address - first) % sizeof(struct Page) == 0);
    index = (address - first) / sizeof(struct Page);
    assert(index < buddy_check_page_count);
    assert(capacity <= buddy_check_page_count - index);
    assert((page2ppn(page) & (capacity - 1)) == 0);
    return index;
}

static uintptr_t
buddy_check_marker(struct Page *page, size_t position) {
    return (uintptr_t)0x2412819a5a5a5a5aULL ^ page2pa(page) ^
           buddy_check_seed ^
           ((uintptr_t)(position + 1) * (uintptr_t)0x9e3779b97f4a7c15ULL);
}

static void
buddy_check_page_data(struct Page *page, bool write) {
    static const size_t offsets[] = {0, PGSIZE / 2,
                                      PGSIZE - sizeof(uintptr_t)};
    size_t position;
    for (position = 0; position < 3; position++) {
        volatile uintptr_t *word = (volatile uintptr_t *)(
            page2pa(page) + va_pa_offset + offsets[position]);
        uintptr_t marker = buddy_check_marker(page, position);
        if (write) {
            *word = marker;
        } else {
            assert(*word == marker);
        }
    }
}

static void
buddy_check_live_data(void) {
    size_t index;
    for (index = 0; index < buddy_check_page_count; index++) {
        if (buddy_check_state[index] & BUDDY_CHECK_LIVE) {
            buddy_check_page_data(pages + index, 0);
        }
    }
}

static void
buddy_check_reserved(void) {
    size_t index;
    for (index = 0; index < buddy_check_page_count; index++) {
        assert((PageReserved(pages + index) != 0) ==
               ((buddy_check_state[index] & BUDDY_CHECK_RESERVED) != 0));
    }
}

static struct Page *
buddy_check_allocate(size_t request) {
    size_t capacity = buddy_check_capacity(request);
    size_t before = nr_free_pages();
    struct Page *page = alloc_pages(request);
    size_t index, offset;

    assert(page != NULL);
    assert(before >= capacity);
    assert(nr_free_pages() == before - capacity);
    index = buddy_check_index(page, capacity);
    /* Check the entire rounded extent, including padding beyond request. */
    for (offset = 0; offset < capacity; offset++) {
        assert(!PageReserved(page + offset));
        assert(!(buddy_check_state[index + offset] &
                 (BUDDY_CHECK_RESERVED | BUDDY_CHECK_LIVE)));
    }
    for (offset = 0; offset < capacity; offset++) {
        buddy_check_state[index + offset] |= BUDDY_CHECK_LIVE;
        buddy_check_page_data(page + offset, 1);
    }
    return page;
}

static void
buddy_check_release(struct Page *page, size_t request) {
    size_t capacity = buddy_check_capacity(request);
    size_t index = buddy_check_index(page, capacity);
    size_t before = nr_free_pages();
    size_t offset;

    assert(capacity <= buddy_check_initial_free);
    assert(before <= buddy_check_initial_free - capacity);
    for (offset = 0; offset < capacity; offset++) {
        assert(buddy_check_state[index + offset] & BUDDY_CHECK_LIVE);
        buddy_check_page_data(page + offset, 0);
    }
    for (offset = 0; offset < capacity; offset++) {
        buddy_check_state[index + offset] &= ~BUDDY_CHECK_LIVE;
    }
    /* Always use the original head and original request, not capacity. */
    free_pages(page, request);
    assert(nr_free_pages() == before + capacity);
}

static void
buddy_check_failure(size_t request) {
    size_t before = nr_free_pages();
    buddy_check_live_data();
    buddy_check_reserved();
    assert(alloc_pages(request) == NULL);
    assert(nr_free_pages() == before);
    buddy_check_live_data();
    buddy_check_reserved();
}

static void
buddy_check_requests(void) {
    static const size_t requests[] = {1, 2, 3, 4, 5, 8, 9};
    static const size_t capacities[] = {1, 2, 4, 4, 8, 8, 16};
    static const size_t release_order[] = {2, 0, 4, 1, 6, 3, 5};
    static struct Page *blocks[7];
    size_t index;

    for (index = 0; index < 7; index++) {
        size_t before = nr_free_pages();
        blocks[index] = buddy_check_allocate(requests[index]);
        /* Literal expected capacities provide an independent rounding oracle. */
        assert(nr_free_pages() == before - capacities[index]);
        buddy_check_live_data();
        if (index == 3) {
            buddy_check_failure(0);
            buddy_check_failure((size_t)-1);
            buddy_check_failure(buddy_check_page_count + 1);
        }
    }
    for (index = 0; index < 7; index++) {
        size_t which = release_order[index];
        buddy_check_release(blocks[which], requests[which]);
        buddy_check_live_data();
    }
    assert(nr_free_pages() == buddy_check_initial_free);
    buddy_check_reserved();
}

/* All free memory has been acquired as single-page allocations.  Find the
 * arena from physical alignment and recorded ownership, never allocation order.
 */
static size_t
buddy_check_find_arena(void) {
    size_t index, offset;
    for (index = 0; index + 16 <= buddy_check_page_count; index++) {
        if ((page2ppn(pages + index) & 15) != 0) {
            continue;
        }
        for (offset = 0; offset < 16; offset++) {
            if (!(buddy_check_state[index + offset] & BUDDY_CHECK_LIVE)) {
                break;
            }
        }
        if (offset == 16) {
            return index;
        }
    }
    assert(0); /* The documented startup precondition requires such an arena. */
    return 0;
}

static void
buddy_check_refill_singles(size_t arena, size_t span, size_t count) {
    size_t index;
    for (index = 0; index < count; index++) {
        struct Page *page = buddy_check_allocate(1);
        size_t allocated = buddy_check_index(page, 1);
        assert(allocated >= arena && allocated - arena < span);
        buddy_check_live_data();
    }
    assert(nr_free_pages() == 0);
}

static void
buddy_check_merging(void) {
    size_t index, arena;
    struct Page *pair, *whole, *first, *second, *third;

    /* Exhaustion isolates the controlled holes from every unrelated free block.
     * A byte per descriptor records ownership without a large stack array.
     */
    for (index = 0; index < buddy_check_initial_free; index++) {
        buddy_check_allocate(1);
    }
    assert(nr_free_pages() == 0);
    buddy_check_live_data();
    buddy_check_failure(1);
    buddy_check_failure(2);
    buddy_check_failure(9);
    arena = buddy_check_find_arena();

    /* These are genuine order-zero buddies by physical page-number XOR. */
    assert((page2ppn(pages + arena) ^ 1) == page2ppn(pages + arena + 1));
    buddy_check_release(pages + arena, 1);
    buddy_check_live_data();
    buddy_check_failure(2);
    buddy_check_release(pages + arena + 1, 1);
    buddy_check_live_data();
    pair = buddy_check_allocate(2);
    assert(pair == pages + arena);
    buddy_check_live_data();
    buddy_check_release(pair, 2);
    buddy_check_live_data();
    buddy_check_refill_singles(arena, 2, 2);

    /* Adjacent pages 1 and 2 cross a buddy boundary; two free pages must not
     * satisfy an aligned two-page allocation, despite sufficient total space.
     */
    assert(page2ppn(pages + arena + 2) ==
           page2ppn(pages + arena + 1) + 1);
    assert((page2ppn(pages + arena + 1) ^ 1) !=
           page2ppn(pages + arena + 2));
    buddy_check_release(pages + arena + 1, 1);
    buddy_check_live_data();
    buddy_check_release(pages + arena + 2, 1);
    buddy_check_live_data();
    assert(nr_free_pages() == 2);
    buddy_check_failure(2);
    buddy_check_refill_singles(arena + 1, 2, 2);

    /* Free alternating physical pages first: even eight free pages cannot
     * satisfy two/eight-page requests while their buddies remain live.
     */
    for (index = 0; index < 16; index += 2) {
        buddy_check_release(pages + arena + index, 1);
        buddy_check_live_data();
    }
    assert(nr_free_pages() == 8);
    buddy_check_failure(2);
    buddy_check_failure(8);
    for (index = 1; index < 16; index += 2) {
        buddy_check_release(pages + arena + index, 1);
        buddy_check_live_data();
    }
    assert(nr_free_pages() == 16);
    whole = buddy_check_allocate(9);
    assert(whole == pages + arena); /* Requires all four merge levels. */
    buddy_check_live_data();
    buddy_check_failure(1);
    buddy_check_release(whole, 9);
    buddy_check_live_data();

    /* Split the merged extent again, including rounded requests released with
     * their original 3/5/3 values.  No choice of allocation order is assumed.
     */
    first = buddy_check_allocate(3);
    buddy_check_live_data();
    second = buddy_check_allocate(5);
    buddy_check_live_data();
    third = buddy_check_allocate(3);
    buddy_check_live_data();
    assert(nr_free_pages() == 0);
    buddy_check_failure(1);
    buddy_check_release(second, 5);
    buddy_check_live_data();
    buddy_check_release(first, 3);
    buddy_check_live_data();
    buddy_check_release(third, 3);
    buddy_check_live_data();
    assert(nr_free_pages() == 16);
    buddy_check_refill_singles(arena, 16, 16);

    /* Every live allocation is now a singleton again.  Check each survivor
     * before its release; do not reset allocator state to conceal a leak.
     */
    buddy_check_live_data();
    for (index = 0; index < buddy_check_page_count; index++) {
        if (buddy_check_state[index] & BUDDY_CHECK_LIVE) {
            buddy_check_release(pages + index, 1);
        }
    }
    assert(nr_free_pages() == buddy_check_initial_free);
    buddy_check_reserved();
}

void
buddy_check(void) {
    size_t index, round;

    assert(pmm_manager == &buddy_pmm_manager);
    assert(npage >= nbase);
    buddy_check_page_count = npage - nbase;
    assert(buddy_check_page_count <= BUDDY_CHECK_MAX_PAGES);
    buddy_check_initial_free = nr_free_pages();
    assert(buddy_check_initial_free >= 64);
    assert(buddy_check_initial_free <= buddy_check_page_count);
    for (index = 0; index < buddy_check_page_count; index++) {
        buddy_check_state[index] = PageReserved(pages + index) ?
                                  BUDDY_CHECK_RESERVED : 0;
    }

    for (round = 0; round < 2; round++) {
        cprintf("buddy_check() round %lu: initial free pages = %lu\n",
                (unsigned long)(round + 1),
                (unsigned long)buddy_check_initial_free);
        buddy_check_seed = (uintptr_t)(round + 1) *
                           (uintptr_t)0xd1b54a32d192ed03ULL;
        buddy_check_failure(0);
        buddy_check_failure((size_t)-1);
        buddy_check_failure(buddy_check_initial_free + 1);
        buddy_check_requests();
        buddy_check_merging();
        for (index = 0; index < buddy_check_page_count; index++) {
            assert(!(buddy_check_state[index] & BUDDY_CHECK_LIVE));
        }
        assert(nr_free_pages() == buddy_check_initial_free);
        buddy_check_reserved();
    }
    cprintf("buddy_check() succeeded!\n");
}

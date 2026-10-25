#include <buddy_pmm.h>
#include <list.h>

/* lab2 2412819: one list per order, with no heap or public Page layout change.
 * Order 15 covers all 32768 pages in the current 128 MiB DRAM window.
 * Only free block heads carry PG_property; their property is the block capacity.
 * Allocated heads instead carry this module-private flag and the original n.
 * Interior pages have neither marker and property == 0.
 */
#define BUDDY_MAX_ORDER 15
#define BUDDY_MAX_PAGES ((size_t)1 << BUDDY_MAX_ORDER)
#define BUDDY_ALLOC_HEAD (1UL << 2)

static list_entry_t buddy_free_lists[BUDDY_MAX_ORDER + 1];
static size_t buddy_free_count;

static size_t
buddy_capacity(unsigned int order) {
    return (size_t)1 << order;
}

/* Reject zero and oversized requests before rounding, so even SIZE_MAX cannot
 * overflow the shift loop. The returned order is the smallest fitting one.
 */
static bool
buddy_request_order(size_t n, unsigned int *order) {
    if (n == 0 || n > BUDDY_MAX_PAGES) {
        return 0;
    }
    *order = 0;
    while (buddy_capacity(*order) < n) {
        (*order)++;
    }
    return 1;
}

/* The caller supplies a valid Page pointer; this is not a wild-pointer guard. */
static bool
buddy_range_valid(struct Page *base, size_t n) {
    if (base < pages || base >= pages + (npage - nbase)) {
        return 0;
    }
    return n <= npage - page2ppn(base);
}

static void
buddy_link_block(struct Page *base, unsigned int order) {
    assert(!PageReserved(base) && !PageProperty(base));
    assert(!(base->flags & BUDDY_ALLOC_HEAD));
    assert((page2ppn(base) & (buddy_capacity(order) - 1)) == 0);
    base->property = buddy_capacity(order);
    SetPageProperty(base);
    list_add(&buddy_free_lists[order], &base->page_link);
}

static void
buddy_unlink_block(struct Page *base) {
    assert(PageProperty(base));
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
}

/* Insert an unlinked free block, coalescing only equal-order XOR buddies.
 * Physical page numbers (not descriptor indices) define alignment and buddies:
 * pages[0] describes nbase, and reserved/unregistered gaps never join a list.
 */
static void
buddy_release_block(struct Page *base, unsigned int order) {
    size_t ppn = page2ppn(base);
    while (order < BUDDY_MAX_ORDER) {
        size_t capacity = buddy_capacity(order);
        size_t partner_ppn = ppn ^ capacity;
        if (partner_ppn < nbase || partner_ppn >= npage ||
            capacity > npage - partner_ppn) {
            break;
        }
        struct Page *partner = pages + (partner_ppn - nbase);
        if (PageReserved(partner) || !PageProperty(partner) ||
            partner->property != capacity) {
            break;
        }
        buddy_unlink_block(partner);
        if (partner_ppn < ppn) {
            base = partner;
            ppn = partner_ppn;
        }
        order++;
    }
    buddy_link_block(base, order);
}

static void
buddy_init(void) {
    /* Fail explicitly if a future platform exceeds the documented capacity. */
    static_assert((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE <=
                  BUDDY_MAX_PAGES);
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_init(&buddy_free_lists[order]);
    }
    buddy_free_count = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(npage >= nbase && npage - nbase <= BUDDY_MAX_PAGES);
    assert(n > 0 && buddy_range_valid(base, n));
    /* Validate the whole interval before changing any descriptor. */
    for (size_t i = 0; i < n; i++) {
        assert(PageReserved(base + i));
    }
    for (size_t i = 0; i < n; i++) {
        struct Page *page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }

    /* Greedy aligned decomposition covers every registered page, including
     * non-power-of-two prefixes/tails. Release can join earlier adjacent maps.
     */
    size_t remaining = n;
    while (remaining != 0) {
        size_t ppn = page2ppn(base);
        unsigned int order = 0;
        while (order < BUDDY_MAX_ORDER) {
            size_t next_capacity = buddy_capacity(order + 1);
            if (next_capacity > remaining || (ppn & (next_capacity - 1))) {
                break;
            }
            order++;
        }
        size_t capacity = buddy_capacity(order);
        buddy_release_block(base, order);
        base += capacity;
        remaining -= capacity;
    }
    buddy_free_count += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    unsigned int requested_order;
    if (!buddy_request_order(n, &requested_order) ||
        buddy_capacity(requested_order) > buddy_free_count) {
        return NULL;
    }
    unsigned int order = requested_order;
    while (order <= BUDDY_MAX_ORDER && list_empty(&buddy_free_lists[order])) {
        order++;
    }
    if (order > BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *base = le2page(list_next(&buddy_free_lists[order]), page_link);
    buddy_unlink_block(base);
    while (order > requested_order) {
        order--;
        buddy_link_block(base + buddy_capacity(order), order);
    }
    /* lab2 2412819: keep internal fragmentation reserved until the matching
     * whole-block free. In particular, alloc(3) consumes four free pages.
     */
    base->property = n;
    base->flags |= BUDDY_ALLOC_HEAD;
    buddy_free_count -= buddy_capacity(requested_order);
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    unsigned int order;
    assert(buddy_request_order(n, &order));
    size_t capacity = buddy_capacity(order);
    assert(buddy_range_valid(base, capacity));
    assert(base->flags & BUDDY_ALLOC_HEAD);
    assert(base->property == n);
    assert((page2ppn(base) & (capacity - 1)) == 0);
    /* Check before mutating: only the recorded head may release an allocation,
     * with exactly its original request, never a rounded count or subrange.
     */
    for (size_t i = 0; i < capacity; i++) {
        struct Page *page = base + i;
        assert(!PageReserved(page) && !PageProperty(page));
        assert(i == 0 || !(page->flags & BUDDY_ALLOC_HEAD));
    }
    for (size_t i = 0; i < capacity; i++) {
        struct Page *page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }
    buddy_release_block(base, order);
    buddy_free_count += capacity;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_free_count;
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "Buddy System",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

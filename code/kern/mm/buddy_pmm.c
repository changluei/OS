#include <buddy_pmm.h>
#include <list.h>

/* lab2 2412819: 2^15 pages cover the current 128 MiB physical window. */
#define BUDDY_MAX_ORDER 15

static list_entry_t buddy_free_lists[BUDDY_MAX_ORDER + 1];
static size_t buddy_free_count;

static size_t
buddy_capacity(unsigned int order) {
    return (size_t)1 << order;
}

/* Reject unsupported requests before rounding, so the shift cannot overflow. */
static bool
buddy_request_order(size_t n, unsigned int *order) {
    if (n == 0 || n > buddy_capacity(BUDDY_MAX_ORDER)) {
        return 0;
    }
    *order = 0;
    while (buddy_capacity(*order) < n) {
        ++*order;
    }
    return 1;
}

static void
buddy_remove_block(struct Page *base) {
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
}

/* Insert an unlisted block; only an actual, equally sized free buddy merges.
 * Counts are updated by callers, not here: merging changes no free capacity.
 */
static void
buddy_insert_block(struct Page *base, unsigned int order) {
    while (order < BUDDY_MAX_ORDER) {
        size_t capacity = buddy_capacity(order);
        ppn_t buddy_ppn = page2ppn(base) ^ capacity;

        /* npage is an exclusive physical PPN, not a descriptor count. */
        if (buddy_ppn < nbase || buddy_ppn >= npage ||
            capacity > npage - buddy_ppn) {
            break;
        }
        struct Page *buddy = pages + (buddy_ppn - nbase);
        if (PageReserved(buddy) || !PageProperty(buddy) ||
            buddy->property != capacity) {
            break;
        }

        buddy_remove_block(buddy);
        if (buddy_ppn < page2ppn(base)) {
            base = buddy;
        }
        ++order;
    }
    base->property = buddy_capacity(order);
    SetPageProperty(base);
    list_add(&buddy_free_lists[order], &base->page_link);
}

static void
buddy_init(void) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        list_init(&buddy_free_lists[order]);
    }
    buddy_free_count = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    assert(npage >= nbase);
    size_t offset = base - pages;
    assert(offset < npage - nbase);
    assert(n <= npage - nbase - offset);

    for (size_t i = 0; i < n; ++i) {
        struct Page *page = base + i;
        assert(PageReserved(page));
        ClearPageReserved(page);
        ClearPageProperty(page);
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }

    buddy_free_count += n;
    while (n != 0) {
        unsigned int order = 0;
        ppn_t ppn = page2ppn(base);

        /* Decompose exactly the supplied range, using physical alignment. */
        while (order < BUDDY_MAX_ORDER) {
            size_t next_capacity = buddy_capacity(order + 1);
            if (next_capacity > n || (ppn & (next_capacity - 1)) != 0) {
                break;
            }
            ++order;
        }
        size_t capacity = buddy_capacity(order);
        buddy_insert_block(base, order);
        base += capacity;
        n -= capacity;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    unsigned int order;
    if (!buddy_request_order(n, &order)) {
        return NULL;
    }
    size_t capacity = buddy_capacity(order);
    if (capacity > buddy_free_count) {
        return NULL;
    }

    unsigned int found = order;
    while (found <= BUDDY_MAX_ORDER &&
           list_empty(&buddy_free_lists[found])) {
        ++found;
    }
    if (found > BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *base =
        le2page(list_next(&buddy_free_lists[found]), page_link);
    buddy_remove_block(base);
    while (found > order) {
        --found;
        struct Page *right = base + buddy_capacity(found);
        right->property = buddy_capacity(found);
        SetPageProperty(right);
        list_add(&buddy_free_lists[found], &right->page_link);
    }
    buddy_free_count -= capacity;
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    /* The caller must supply the original head and original request, not an
     * arbitrary subrange. For example, a request of 3 returns/frees 4 pages.
     */
    unsigned int order = 0;
    bool valid_request = buddy_request_order(n, &order);
    assert(valid_request);
    assert(npage >= nbase);
    size_t capacity = buddy_capacity(order);
    size_t offset = base - pages;
    assert(offset < npage - nbase);
    assert(capacity <= npage - nbase - offset);
    assert((page2ppn(base) & (capacity - 1)) == 0);

    for (size_t i = 0; i < capacity; ++i) {
        struct Page *page = base + i;
        assert(!PageReserved(page) && !PageProperty(page));
        page->property = 0;
        set_page_ref(page, 0);
    }
    buddy_insert_block(base, order);
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

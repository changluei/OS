#include <pmm.h>
#include <list.h>
#include <best_fit_pmm.h>
#include <stdio.h>
#include <slub.h>  // 为学生新增的组合自检提供 SLUB 检查入口。

//lab2 function2-2 2412819

// 本管理器独立维护空闲块链表和空闲页总数，避免与其他分配器共享内部状态。
static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void
best_fit_init(void) {
    // 此时只初始化管理结构，实际可用页面随后由 init_memmap 登记。
    list_init(&free_list);
    nr_free = 0;
}

/* 按地址顺序登记空闲块，并尝试与前后相邻块合并。
 * 空闲页数只增加本次新归还的 n 页，合并已有块时不重复计数。 */
static void
best_fit_insert_block(struct Page *base, size_t n) {
    // 找到第一个起点大于 base 的块，插入它之前，保持地址有序。
    list_entry_t *le = list_next(&free_list);
    while (le != &free_list && le2page(le, page_link) < base) {
        le = list_next(le);
    }
    // 只有空闲块的头页设置 PG_property，property 记录该块连续页面的数量。
    base->property = n;
    SetPageProperty(base);
    list_add_before(le, &base->page_link);
    nr_free += n;

    // 前块末端等于当前起点，说明物理连续；合并后以前块作为新的块头。
    le = list_prev(&base->page_link);
    if (le != &free_list) {
        struct Page *prev = le2page(le, page_link);
        if (prev + prev->property == base) {
            prev->property += base->property;
            ClearPageProperty(base);
            base->property = 0;
            list_del(&base->page_link);
            base = prev;
        }
    }

    // 再检查后块；被合并的旧块头须清除标志并摘链，避免重复登记。
    le = list_next(&base->page_link);
    if (le != &free_list) {
        struct Page *next = le2page(le, page_link);
        if (base + base->property == next) {
            base->property += next->property;
            ClearPageProperty(next);
            next->property = 0;
            list_del(&next->page_link);
        }
    }
}

static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    // 初始化阶段传入的页应仍处于保留状态；清理状态后交给统一登记函数。
    for (struct Page *p = base; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    best_fit_insert_block(base, n);
}

static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    // 总空闲页数不足时直接失败，避免继续扫描链表。
    if (n > nr_free) {
        return NULL;
    }

    struct Page *best = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        /* Best-Fit 扫描所有能容纳 n 页的块，选择容量最小者。
         * 同样大小时不替换候选，因此地址有序链表会保留起点较小的块。 */
        if (p->property >= n &&
            (best == NULL || p->property < best->property)) {
            best = p;
        }
    }
    // 总空闲页足够也可能因外部碎片，找不到足够大的连续块。
    if (best == NULL) {
        return NULL;
    }

    // 从所选块头取走 n 页，剩余部分作为新空闲块，保留在原链表位置。
    size_t remaining = best->property - n;
    list_entry_t *prev = list_prev(&best->page_link);
    list_del(&best->page_link);
    if (remaining > 0) {
        struct Page *rest = best + n;
        rest->property = remaining;
        SetPageProperty(rest);
        list_add(prev, &rest->page_link);
    }
    // 已分配的头页不再表示空闲块，计数只扣除实际取走的 n 页。
    ClearPageProperty(best);
    best->property = 0;
    nr_free -= n;
    return best;
}

static void
best_fit_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    // 归还的页面不能是保留页或已登记的空闲块头；清理其元数据后统一插入、合并。
    for (struct Page *p = base; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    best_fit_insert_block(base, n);
}

static size_t
best_fit_nr_free_pages(void) {
    return nr_free;
}

// Framework tests: students must not modify basic_check or best_fit_check.
static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
best_fit_check(void) {
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
    free_pages(p0 + 4, 1);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
    assert(alloc_pages(2) != NULL);
    assert(p0 + 4 == p1);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    p2 = p0 + 1;
    free_pages(p0, 5);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
}

/* lab2 2412439：学生新增的组合自检，不改框架入口或原有测试函数。
 * 先验证页面分配，再验证上层对象分配及回收。 */
static void
best_fit_and_slub_check(void) {
    best_fit_check();
    assert(slub_check() == SLUB_OK);
}

// 把学生实现注册到课程统一接口；框架仍通过这些函数指针调用管理器。
const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_and_slub_check,
};

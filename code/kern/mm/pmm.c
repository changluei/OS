#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;  // 按实验要求启用 Best-Fit，后续通过统一接口分配页面。
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2412439
/* 建立物理页描述符，排除已有占用，再把完整的空闲页面交给分配器。 */
static void page_init(void) {
    extern char end[];
    // DTB 解析已由框架完成；这里只读取其报告的 RAM 起点和长度。
    uint64_t memory_base = get_memory_base();
    uint64_t memory_size = get_memory_size();

    // 拒绝空范围，并在计算 base + size 前检查加法是否会溢出。
    if (memory_base == 0 || memory_size == 0 ||
        memory_size > ~(uint64_t)0 - memory_base) {
        panic("invalid physical memory range from DTB");
    }

    // RAM 按左闭右开区间处理；管理末端受框架上限约束，并舍去不足一页的尾部。
    uint64_t memory_end = memory_base + memory_size;
    uintptr_t managed_end = memory_end < PHYSICAL_MEMORY_END
                                ? memory_end : PHYSICAL_MEMORY_END;
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;  // 内核虚拟地址减去该偏移后得到物理地址。

    // end 是虚拟地址，须先转为物理地址，才能判断 RAM 是否覆盖已加载的内核。
    uintptr_t kernel_end = PADDR((uintptr_t)end);
    if (memory_base > KERNEL_BEGIN_PADDR ||
        managed_end <= kernel_end || managed_end <= DRAM_BASE) {
        panic("physical memory does not contain the kernel");
    }

    // npage 是物理页号的排他上界；实际需要 npage - nbase 个描述符。
    npage = PPN(managed_end);
    // 描述符数组紧跟内核镜像，起点向上按页对齐；空间核算统一使用物理地址。
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR((uintptr_t)pages);
    size_t descriptor_bytes = (npage - nbase) * sizeof(struct Page);
    // 描述符本身也占 RAM，必须确认放得下之后才能写入，避免越过管理边界。
    if (pages_pa > managed_end ||
        descriptor_bytes > managed_end - pages_pa) {
        panic("not enough physical memory for Page descriptors");
    }

    // 先将所有页面标为保留，清空引用计数和块信息；后续仅登记真正可用的区间。
    for (struct Page *page = pages; page < pages + (npage - nbase); page++) {
        page->flags = 0;
        SetPageReserved(page);
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }

    // 跳过整个描述符数组及对齐空隙，剩余完整页面才允许分配。
    uintptr_t free_begin = ROUNDUP(pages_pa + descriptor_bytes, PGSIZE);
    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            memory_size, memory_base, memory_end - 1);
    cprintf("  managed end: 0x%016lx, free begin: 0x%016lx\n",
            managed_end, free_begin);
    // 空闲区非空时才注册；pa2page 将物理起点转换成对应的 Page 描述符指针。
    if (free_begin < managed_end) {
        init_memmap(pa2page(free_begin), (managed_end - free_begin) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

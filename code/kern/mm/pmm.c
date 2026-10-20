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
// Exclusive physical page-number upper bound; descriptor count is npage - nbase.
size_t npage = 0;
// Kernel virtual address = physical address + va_pa_offset.
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
#ifdef PMM_FIRST_FIT
    pmm_manager = &default_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
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
static void page_init(void) {
    extern char end[];
    uint64_t memory_base = get_memory_base();
    uint64_t memory_size = get_memory_size();

    if (memory_base == 0 || memory_size == 0 ||
        memory_size > ~(uint64_t)0 - memory_base) {
        panic("invalid physical memory range from DTB");
    }

    uint64_t memory_end = memory_base + memory_size;
    uintptr_t managed_end = memory_end < PHYSICAL_MEMORY_END
                                ? memory_end : PHYSICAL_MEMORY_END;
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // The reported RAM must contain the loaded kernel and its descriptors.
    uintptr_t kernel_end = PADDR((uintptr_t)end);
    if (memory_base > KERNEL_BEGIN_PADDR ||
        managed_end <= kernel_end || managed_end <= DRAM_BASE) {
        panic("physical memory does not contain the kernel");
    }

    npage = PPN(managed_end);
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR((uintptr_t)pages);
    size_t descriptor_bytes = (npage - nbase) * sizeof(struct Page);
    if (pages_pa > managed_end ||
        descriptor_bytes > managed_end - pages_pa) {
        panic("not enough physical memory for Page descriptors");
    }

    // Reserve every described page before exposing any page to the allocator.
    for (struct Page *page = pages; page < pages + (npage - nbase); page++) {
        page->flags = 0;
        SetPageReserved(page);
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }

    uintptr_t free_begin = ROUNDUP(pages_pa + descriptor_bytes, PGSIZE);
    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            memory_size, memory_base, memory_end - 1);
    cprintf("  managed end: 0x%016lx, free begin: 0x%016lx\n",
            managed_end, free_begin);
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

#!/usr/bin/env python3
"""Exercise the real page_init body with mocked RAM/DTB on the host.

Only the linker-provided `end` declaration is replaced with a test address.
The allocator and console are test doubles; allocation algorithms and RISC-V
execution are checked separately in QEMU. Course grading files are untouched.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def page_init_source(path):
    text = path.read_text()
    match = re.search(r"static\s+void\s+page_init\s*\(void\)\s*\{", text)
    if match is None:
        raise RuntimeError("page_init definition not found")
    depth = 0
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]', re.S)
    for token in tokens.finditer(text, match.end() - 1):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                body = text[match.start():token.end()]
                body, count = re.subn(
                    r"extern\s+char\s+end\[\];",
                    "const char *end = test_kernel_end;", body,
                )
                if count != 1:
                    raise RuntimeError("linker end declaration changed")
                return body
    raise RuntimeError("page_init definition is incomplete")


PREFIX = r'''
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PGSIZE 4096UL
#define DRAM_BASE 0x80000000UL
#define KERNEL_BEGIN_PADDR 0x80200000UL
#define PHYSICAL_MEMORY_END 0x88000000UL
#define PHYSICAL_MEMORY_OFFSET ((uintptr_t)test_ram - DRAM_BASE)
#define PPN(a) ((uintptr_t)(a) / PGSIZE)
#define ROUNDDOWN(a, n) ((a) - (a) % (n))
#define ROUNDUP(a, n) ROUNDDOWN((a) + (n) - 1, (n))
#define PADDR(a) ((uintptr_t)(a) - va_pa_offset)
#define SetPageReserved(p) ((p)->flags |= 1UL)
#define PageReserved(p) ((p)->flags & 1UL)

typedef struct list_entry {
    struct list_entry *prev, *next;
} list_entry_t;
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};
static unsigned char *test_ram;
static const char *test_kernel_end;
static struct Page *pages;
static size_t npage;
static const size_t nbase = DRAM_BASE / PGSIZE;
static uint64_t va_pa_offset;
static uint64_t test_base, test_size;
static size_t map_calls, mapped_pages;
static uintptr_t mapped_begin;
static jmp_buf panic_target;

static uint64_t get_memory_base(void) { return test_base; }
static uint64_t get_memory_size(void) { return test_size; }
static void set_page_ref(struct Page *p, int ref) { p->ref = ref; }
static void list_init(list_entry_t *link) { link->prev = link->next = link; }
static int cprintf(const char *format, ...) { (void)format; return 0; }
static void panic(const char *message) {
    (void)message;
    longjmp(panic_target, 1);
}
static struct Page *pa2page(uintptr_t pa) {
    assert(PPN(pa) >= nbase && PPN(pa) < npage);
    return pages + PPN(pa) - nbase;
}
static void init_memmap(struct Page *base, size_t count) {
    assert(count > 0);
    assert(map_calls == 0);
    // All descriptors must be initialized and reserved before registration.
    for (struct Page *p = pages; p < pages + npage - nbase; p++) {
        assert(PageReserved(p));
        assert(p->ref == 0 && p->property == 0);
        assert(p->page_link.prev == &p->page_link);
        assert(p->page_link.next == &p->page_link);
    }
    map_calls++;
    mapped_begin = DRAM_BASE + (base - pages) * PGSIZE;
    mapped_pages = count;
    for (size_t i = 0; i < count; i++) {
        base[i].flags = 0;
    }
}
'''


SUFFIX = r'''
static void run_case(const char *name, uint64_t base, uint64_t size,
                     int expect_panic, uintptr_t expected_end,
                     uintptr_t expected_begin, size_t expected_pages) {
    memset(test_ram, 0xa5, 8UL << 20);
    test_kernel_end = (const char *)test_ram + 0x200123;
    test_base = base;
    test_size = size;
    pages = NULL;
    npage = 0;
    va_pa_offset = 0;
    map_calls = mapped_pages = mapped_begin = 0;
    if (setjmp(panic_target)) {
        assert(expect_panic);
        assert(map_calls == 0);
        printf("PASS: %s (rejected before free-page registration)\n", name);
        return;
    }
    page_init();
    assert(!expect_panic);
    assert(va_pa_offset == PHYSICAL_MEMORY_OFFSET);
    assert(npage == expected_end / PGSIZE);
    assert(PADDR(pages) == 0x80201000UL);
    assert(mapped_pages == expected_pages);
    assert(map_calls == (expected_pages != 0));
    if (expected_pages != 0) {
        assert(mapped_begin == expected_begin);
        assert(mapped_begin + mapped_pages * PGSIZE == expected_end);
    }
    // The kernel and every page occupied by descriptors stay reserved.
    assert(PageReserved(pa2page(KERNEL_BEGIN_PADDR)));
    assert(PageReserved(pa2page(PADDR(test_kernel_end))));
    assert(PageReserved(pa2page(PADDR(pages))));
    for (size_t i = 0; i < npage - nbase; i++) {
        uintptr_t pa = DRAM_BASE + i * PGSIZE;
        assert(!!PageReserved(pages + i) == (pa < expected_begin));
    }
    printf("PASS: %s (%zu free pages)\n", name, mapped_pages);
}

int main(void) {
    assert(sizeof(struct Page) == 40);
    assert(posix_memalign((void **)&test_ram, PGSIZE, 8UL << 20) == 0);
    run_case("128 MiB RAM", DRAM_BASE, 128UL << 20,
             0, 0x88000000, 0x80341000, 31935);
    run_case("16 MiB RAM", DRAM_BASE, 16UL << 20,
             0, 0x81000000, 0x80229000, 3543);
    run_case("partial final page", DRAM_BASE, (16UL << 20) + 2047,
             0, 0x81000000, 0x80229000, 3543);
    run_case("256 MiB capped at framework limit", DRAM_BASE, 256UL << 20,
             0, 0x88000000, 0x80341000, 31935);
    run_case("4 MiB RAM", DRAM_BASE, 4UL << 20,
             0, 0x80400000, 0x8020b000, 501);
    run_case("descriptors fit but no complete free page", DRAM_BASE, 0x207000,
             0, 0x80207000, 0x80207000, 0);
    run_case("zero RAM base", 0, 128UL << 20, 1, 0, 0, 0);
    run_case("zero RAM size", DRAM_BASE, 0, 1, 0, 0, 0);
    run_case("RAM range overflow", DRAM_BASE, UINT64_MAX, 1, 0, 0, 0);
    run_case("RAM misses kernel start", 0x80400000, 16UL << 20,
             1, 0, 0, 0);
    run_case("RAM ends before kernel end", DRAM_BASE, 2UL << 20,
             1, 0, 0, 0);
    run_case("descriptor array does not fit", DRAM_BASE, 0x206000,
             1, 0, 0, 0);
    free(test_ram);
    puts("12 page_init boundary cases passed.");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="host C compiler")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "kern/mm/pmm.c"
    with tempfile.TemporaryDirectory(prefix="lab2-page-init-") as directory:
        directory = Path(directory)
        harness = directory / "page_init_test.c"
        executable = directory / "page_init_test"
        harness.write_text(PREFIX + page_init_source(source) + SUFFIX)
        subprocess.run([
            args.cc, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            str(harness), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
    sd a0, 0(t0)
    la t0, boot_dtb
    sd a1, 0(t0)
    # Paging is off, so the PC-relative address is physical here.
    la t0, boot_page_table_sv39
    srli t0, t0, 12
    li t1, 8 << 60
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    # The temporary identity mapping covers the instructions before this jump.
    lui t0, %hi(.Lvirtual_entry)
    addi t0, t0, %lo(.Lvirtual_entry)
    jr t0

.Lvirtual_entry:
    la t0, boot_page_table_sv39
    sd zero, 2 * 8(t0)
    sfence.vma
    la sp, bootstacktop
    tail kern_init

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:

.section .data
    # 由于我们要把这个页表放到一个页里面，因此必须 12 位对齐
    .align PGSHIFT
    .global boot_page_table_sv39
# 分配 4KiB 内存给预设的三级页表
boot_page_table_sv39:
    # Root entry 2 temporarily maps 0x80000000 to itself during the switch.
    .zero 8 * 2
    .quad (0x80000 << 10) | 0xcf
    .zero 8 * (511 - 3)
    # The permanent kernel mapping covers the same physical GiB.
    .quad (0x80000 << 10) | 0xcf # VRWXAD
    .global boot_hartid
boot_hartid:
    .quad 0
    .global boot_dtb
boot_dtb:
    .quad 0

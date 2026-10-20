#!/usr/bin/env bash
# 2412439: independent initialization verification; run inside Linux/WSL.
set -euo pipefail
cd "$(dirname "$0")/.."
qemu_bin="${QEMU:-qemu-system-riscv64}"
gdb_bin="${GDB:-gdb-multiarch}"
check_dir="$(mktemp -d)"
qemu_pid=""
cleanup() {
    if [[ -n "$qemu_pid" ]]; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
    fi
    rm -rf "$check_dir"
}
trap cleanup EXIT

printf '%s\n' '=== Environment ==='
uname -m
riscv64-unknown-elf-gcc --version | head -1
"$qemu_bin" --version | head -1
"$gdb_bin" --version | head -1
printf '%s\n' '=== page_init host boundary checks ==='
python3 tools/check_page_init.py
printf '%s\n' '=== Build: PMM=first_fit ==='
make PMM=first_fit -j2
printf '%s\n' '=== Backend selection and object rebuild (no Best-Fit implementation test) ==='
make PMM=best_fit obj/kern/mm/pmm.o
riscv64-unknown-elf-objdump -r obj/kern/mm/pmm.o > "$check_dir/pmm.reloc"
grep -q best_fit_pmm_manager "$check_dir/pmm.reloc"
make PMM=first_fit -j2
riscv64-unknown-elf-objdump -r obj/kern/mm/pmm.o > "$check_dir/pmm.reloc"
grep -q default_pmm_manager "$check_dir/pmm.reloc"
if grep -q best_fit_pmm_manager "$check_dir/pmm.reloc"; then exit 1; fi
printf '%s\n' 'PASS: backend switches rebuild pmm.o with the intended manager reference.'
riscv64-unknown-elf-readelf -h bin/kernel | sed -n '/Class:/p;/Machine:/p;/Entry point/p'

for ram in 64 128 256; do
    printf '=== QEMU: %s MiB RAM, First-Fit ===\n' "$ram"
    run_status=0
    timeout 5s "$qemu_bin" -machine virt -m "${ram}M" -nographic \
        -bios default -device loader,file=bin/ucore.img,addr=0x80200000 \
        > "$check_dir/boot.log" 2>&1 || run_status=$?
    cat "$check_dir/boot.log"
    if [[ "$run_status" -ne 0 && "$run_status" -ne 124 ]]; then
        exit "$run_status"
    fi
    grep -Fq 'memory management: default_pmm_manager' "$check_dir/boot.log"
    grep -Fq 'check_alloc_page() succeeded!' "$check_dir/boot.log"
    if grep -Eq 'kernel panic|FAIL' "$check_dir/boot.log"; then exit 1; fi
    if [[ "$ram" -eq 64 ]]; then
        grep -Fq 'managed end: 0x0000000084000000' "$check_dir/boot.log"
    else
        grep -Fq 'managed end: 0x0000000088000000' "$check_dir/boot.log"
    fi
    printf 'PASS: %s MiB boot, initialization and existing First-Fit checks\n' "$ram"
done

cat > "$check_dir/init.gdb" <<'GDB'
set pagination off
set architecture riscv:rv64
set tcp auto-retry on
set tcp connect-timeout 5
target remote 127.0.0.1:1236
tbreak default_check
continue
info registers pc satp
x/6i $pc
python
import gdb
def value(expression):
    return int(gdb.parse_and_eval(expression))
offset = value("va_pa_offset")
npage = value("npage")
nbase = value("nbase")
pages = value("(unsigned long)pages")
kernel_end = value("(unsigned long)&end")
page_size = value("sizeof(struct Page)")
assert offset == 0xffffffff40000000
assert npage == 0x88000 and nbase == 0x80000
assert pages == (kernel_end + 4095) & ~4095
free_begin = ((pages - offset) + (npage - nbase) * page_size + 4095) & ~4095
expected_free = (0x88000000 - free_begin) // 4096
assert value("free_area.nr_free") == expected_free
assert value("pages[0].flags") & 1
assert value("pages[((unsigned long)&end - va_pa_offset) / 4096 - nbase].flags") & 1
assert value("pages[((unsigned long)pages - va_pa_offset) / 4096 - nbase].flags") & 1
root = value("(unsigned long)&boot_page_table_sv39")
satp = value("$satp") & ((1 << 64) - 1)
assert satp >> 60 == 8
assert (satp & ((1 << 44) - 1)) << 12 == root - offset
virtual = value("(unsigned long)&kern_init")
index = (virtual >> 30) & 511
pte = value("((unsigned long *)&boot_page_table_sv39)[%d]" % index)
assert index == 511 and pte & 0xcf == 0xcf
physical = ((pte >> 10) << 12) + (virtual & ((1 << 30) - 1))
assert physical == virtual - offset
print("PASS: initialized descriptors, reserved pages and %d free pages" % expected_free)
print("Sv39 root VA=%#x PA=%#x satp=%#x" % (root, root - offset, satp))
print("Sv39 VPN[2]=%d PTE=%#x (1 GiB leaf)" % (index, pte))
print("PASS: kern_init VA=%#x -> PA=%#x" % (virtual, physical))
end
detach
quit
GDB

printf '%s\n' '=== GDB: page state, SATP and Sv39 mapping ==='
"$qemu_bin" -machine virt -m 128M -nographic -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000 \
    -gdb tcp:127.0.0.1:1236 -S > "$check_dir/debug.log" 2>&1 &
qemu_pid=$!
gdb_status=0
timeout 30s "$gdb_bin" -q -batch bin/kernel -x "$check_dir/init.gdb" \
    > "$check_dir/gdb.log" 2>&1 || gdb_status=$?
cat "$check_dir/gdb.log"
if [[ "$gdb_status" -ne 0 ]]; then exit "$gdb_status"; fi
grep -Fq 'PASS: initialized descriptors' "$check_dir/gdb.log"
grep -Fq 'PASS: kern_init VA=' "$check_dir/gdb.log"
printf '%s\n' 'PASS: independent initialization verification completed.'
printf '%s\n' 'NOTE: First-Fit verification; course Best-Fit grading and Buddy/SLUB are separate.'

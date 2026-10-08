#!/usr/bin/env bash
set -euo pipefail
cd /workspace
make
riscv64-unknown-elf-readelf -h bin/kernel | grep -E 'Class:|Machine:|Entry point'
lab_check_dir="$(mktemp -d)"
lab_qemu_pid=""
cleanup() {
    if [[ -n "$lab_qemu_pid" ]]; then
        kill "$lab_qemu_pid" 2>/dev/null || true
        wait "$lab_qemu_pid" 2>/dev/null || true
    fi
    rm -rf "$lab_check_dir"
}
trap cleanup EXIT
lab_run_status=0
timeout 8s make qemu > "$lab_check_dir/boot.log" 2>&1 || lab_run_status=$?
cat "$lab_check_dir/boot.log"
if [[ "$lab_run_status" -ne 0 && "$lab_run_status" -ne 124 ]]; then exit "$lab_run_status"; fi
grep -F '(THU.CST) os is loading ...' "$lab_check_dir/boot.log"
echo 'PASS: QEMU 启动内核并输出预期字符串（内核随后按设计进入死循环）。'
qemu-system-riscv64 -machine virt -nographic -bios default \
    -kernel bin/ucore.img \
    -gdb tcp:127.0.0.1:1235 -S > "$lab_check_dir/debug.log" 2>&1 &
lab_qemu_pid=$!
timeout 25s gdb-multiarch -q -batch bin/kernel \
    -ex 'set pagination off' \
    -ex 'set architecture riscv:rv64' \
    -ex 'set tcp auto-retry on' \
    -ex 'set tcp connect-timeout 5' \
    -ex 'target remote 127.0.0.1:1235' \
    -ex 'info registers pc' \
    -ex 'x/8i 0x1000' \
    -ex 'tbreak *0x80000000' \
    -ex 'continue' \
    -ex 'info registers pc' \
    -ex 'tbreak *kern_entry' \
    -ex 'continue' \
    -ex 'info registers pc sp' \
    -ex 'x/5i $pc' \
    -ex 'detach' > "$lab_check_dir/gdb.log" 2>&1
cat "$lab_check_dir/gdb.log"
grep -E 'pc[[:space:]]+0x1000' "$lab_check_dir/gdb.log"
grep -E 'pc[[:space:]]+0x80000000' "$lab_check_dir/gdb.log"
grep -E 'pc[[:space:]]+0x80200000.*kern_entry' "$lab_check_dir/gdb.log"
echo 'PASS: GDB 连接、复位 ROM、OpenSBI 入口和内核入口断点均已验证。'
if [[ ! -f tools/grade.sh ]]; then
    echo 'INFO: 本次初始代码未提供 tools/grade.sh，make grade 尚不可用。'
fi

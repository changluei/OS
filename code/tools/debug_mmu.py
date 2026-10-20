#!/usr/bin/env python3
"""2412439: trace QEMU 4.1.1 MMU/TLB and the matching guest DTB load.

Run inside Linux with an unstripped QEMU build and its source directory.
The two GDB traces use separate QEMU runs so debugger memory reads do not get
mistaken for guest CPU accesses in the host MMU trace. No course tests change.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


HOST_GDB = r'''
set pagination off
set confirm off
handle SIGPIPE nostop noprint pass
break get_physical_address if addr == 0x80200000 && access_type == 2
run
printf "\n=== Bare-mode instruction fetch ===\n"
print/x addr
print/x env->satp
print mmu_idx
set $physical_ptr = physical
finish
print/x *$physical_ptr
python
import gdb
assert int(gdb.parse_and_eval("*$physical_ptr")) == 0x80200000
print("PASS: bare-mode VA=PA=0x80200000")
end
disable 1
break tlb_set_page_with_attrs if vaddr == 0x80200000
continue
print/x vaddr
print/x paddr
print mmu_idx
print/x size
printf "PASS: QEMU fills its software TLB even in bare mode\n"
disable 2
break get_physical_address if addr == 0xffffffffc2200000 && access_type == 0
continue
printf "\n=== Real guest load, Sv39 translation ===\n"
bt 6
info args
print/x env->satp
set $physical_ptr = physical
set $prot_ptr = prot
set $addr = addr
up
set $arch_cpu = (RISCVCPU *)cs
down
set $tlb_index = ($addr >> 12) & ($arch_cpu->neg.tlb.f[1].mask / sizeof(CPUTLBEntry))
set $entry = &$arch_cpu->neg.tlb.f[1].table[$tlb_index]
print/x $entry->addr_read
python
tag = int(gdb.parse_and_eval("$entry->addr_read")) & ((1 << 64) - 1)
assert tag & ~4095 != 0xffffffffc2200000
print("PASS: software TLB read tag misses the requested page")
end
disable 3
tbreak __WALK_LOCATION__
continue
printf "\n=== Single-step page-table leaf handling ===\n"
print levels
print i
print ptshift
print/x pte_addr
print/x pte
python
assert int(gdb.parse_and_eval("levels")) == 3
assert int(gdb.parse_and_eval("i")) == 0
assert int(gdb.parse_and_eval("pte")) == 0x200000cf
print("PASS: Sv39 root is already a 1 GiB leaf; lower levels are not walked")
end
next
next
finish
print/x *$physical_ptr
print/x *$prot_ptr
python
assert int(gdb.parse_and_eval("*$physical_ptr")) == 0x82200000
print("PASS: actual QEMU translation VA=0xffffffffc2200000 -> PA=0x82200000")
end
break tlb_set_page_with_attrs if vaddr == 0xffffffffc2200000
continue
printf "\n=== Software TLB refill ===\n"
info args
finish
set $tlb_index = ($addr >> 12) & ($arch_cpu->neg.tlb.f[1].mask / sizeof(CPUTLBEntry))
set $entry = &$arch_cpu->neg.tlb.f[1].table[$tlb_index]
print/x $entry->addr_read
print/x $entry->addr_code
print/x $entry->addend
python
mask = (1 << 64) - 1
tag = int(gdb.parse_and_eval("$entry->addr_read")) & mask
assert tag == 0xffffffffc2200000
addend = int(gdb.parse_and_eval("$entry->addend")) & mask
host_address = (0xffffffffc2200000 + addend) & mask
data = gdb.selected_inferior().read_memory(host_address, 4).tobytes()
assert data == bytes.fromhex("d00dfeed")
print("PASS: refilled TLB points to host RAM containing DTB magic d00dfeed")
end
quit
'''


GUEST_GDB = r'''
set pagination off
set confirm off
set architecture riscv:rv64
set tcp auto-retry on
set tcp connect-timeout 5
target remote 127.0.0.1:__PORT__
tbreak __LOAD_LOCATION__
continue
python
import gdb
import re
mask = (1 << 64) - 1
target = (int(gdb.parse_and_eval("boot_dtb")) + 0xffffffff40000000) & mask
for step in range(100):
    pc = int(gdb.parse_and_eval("$pc")) & mask
    instruction = gdb.selected_frame().architecture().disassemble(pc, count=1)[0]["asm"]
    match = re.search(r"\b(?:lw|lwu)\s+(\w+),\s*(-?\d+)\((\w+)\)", instruction)
    if match:
        dest, displacement, base = match.groups()
        address = (int(gdb.parse_and_eval("$" + base)) + int(displacement)) & mask
        if address == target:
            print("Guest DTB load: PC=%#x instruction=%s VA=%#x" % (pc, instruction, address))
            gdb.execute("si")
            loaded = int(gdb.parse_and_eval("$" + dest)) & 0xffffffff
            assert loaded == 0xedfe0dd0
            print("PASS: guest lw/lwu read DTB bytes d00dfeed (little-endian value %#x)" % loaded)
            break
    gdb.execute("si", to_string=True)
else:
    raise RuntimeError("DTB magic load was not reached within 100 instructions")
end
detach
quit
'''


def line_containing(path, needle):
    matches = [number for number, line in enumerate(path.read_text().splitlines(), 1)
               if needle in line]
    if len(matches) != 1:
        raise RuntimeError(f"source anchor missing or ambiguous: {needle}")
    return f"{path}:{matches[0]}"


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=60)
    print(result.stdout, end="", flush=True)
    result.check_returncode()
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu-source", type=Path, required=True)
    parser.add_argument("--gdb", default="gdb-multiarch")
    parser.add_argument("--port", type=int, default=1237)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = args.qemu_source.resolve()
    qemu = source / "riscv64-softmmu/qemu-system-riscv64"
    if not qemu.is_file():
        parser.error(f"unstripped QEMU build not found: {qemu}")
    walk = line_containing(source / "target/riscv/cpu_helper.c",
                           "target_ulong ppn = pte >> PTE_PPN_SHIFT;")
    load = line_containing(root / "kern/driver/dtb.c",
                           "uint32_t magic = fdt32_to_cpu(header->magic);")
    run(["make", "PMM=first_fit", "-j2"], root)
    qemu_args = [str(qemu), "-machine", "virt", "-m", "128M", "-nographic",
                 "-bios", "default", "-device",
                 "loader,file=bin/ucore.img,addr=0x80200000"]
    with tempfile.TemporaryDirectory(prefix="lab2-mmu-") as directory:
        directory = Path(directory)
        host_script = directory / "host.gdb"
        host_script.write_text(HOST_GDB.replace("__WALK_LOCATION__", walk))
        print("=== Host GDB: QEMU 4.1.1 native MMU/TLB implementation ===", flush=True)
        host_output = run([args.gdb, "-q", "-batch", "-x", str(host_script),
                           "--args", *qemu_args], root)
        assert "PASS: refilled TLB points to host RAM" in host_output

        guest_script = directory / "guest.gdb"
        guest_script.write_text(GUEST_GDB.replace("__PORT__", str(args.port))
                                .replace("__LOAD_LOCATION__", load))
        print("=== Guest GDB: corresponding RISC-V DTB load instruction ===", flush=True)
        with (directory / "qemu.log").open("w") as output:
            process = subprocess.Popen(
                [*qemu_args, "-gdb", f"tcp:127.0.0.1:{args.port}", "-S"],
                cwd=root, stdin=subprocess.DEVNULL, stdout=output,
                stderr=subprocess.STDOUT,
            )
            try:
                guest_output = run([args.gdb, "-q", "-batch", "bin/kernel",
                                    "-x", str(guest_script)], root)
                assert "PASS: guest lw/lwu read DTB bytes" in guest_output
            finally:
                process.terminate()
                process.wait(timeout=5)
    print("PASS: QEMU source MMU/TLB and guest instruction traces completed.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Verify Lab2 allocators in disposable copies of the course framework.

The repository keeps the original Makefile and kernel entry. Alternate
managers and additional test calls are inserted only in temporary copies.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


MANAGERS = {
    "first_fit": "default_pmm_manager",
    "best_fit": "best_fit_pmm_manager",
    "buddy": "buddy_pmm_manager",
}


def prepare_tree(source, destination, backend, slub_check=True):
    source, destination = Path(source), Path(destination)
    destination.mkdir(parents=True)
    shutil.copy2(source / "Makefile", destination / "Makefile")
    for name in ("kern", "libs", "tools"):
        shutil.copytree(source / name, destination / name,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    pmm = destination / "kern/mm/pmm.c"
    text = pmm.read_text()
    anchor = "    pmm_manager = &best_fit_pmm_manager;"
    if text.count(anchor) != 1:
        raise RuntimeError("expected the normal Best-Fit manager selection")
    text = text.replace(anchor, "    pmm_manager = &" + MANAGERS[backend] + ";")
    if backend == "buddy":
        text = "#include <buddy_pmm.h>\n" + text
    pmm.write_text(text)
    # The normal Best-Fit student callback already runs SLUB. For the two
    # alternate managers, add an equivalent call in this disposable tree.
    if slub_check and backend != "best_fit":
        init = destination / "kern/init/init.c"
        text = init.read_text()
        anchor = "    pmm_init();"
        if text.count(anchor) != 1:
            raise RuntimeError("kernel initialization anchor changed")
        text = "#include <slub.h>\n" + text
        text = text.replace(anchor, anchor + "\n    assert(slub_check() == SLUB_OK);")
        init.write_text(text)


def run_build(tree):
    subprocess.run(["make", "-j2"], cwd=tree, check=True)


def run_kernel(tree, backend, ram, qemu):
    command = [qemu, "-machine", "virt", "-m", f"{ram}M", "-nographic",
               "-bios", "default", "-device",
               "loader,file=bin/ucore.img,addr=0x80200000"]
    try:
        result = subprocess.run(command, cwd=tree, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=4)
        output = result.stdout
        if result.returncode != 0:
            raise RuntimeError(f"QEMU exited with {result.returncode}: {output!r}")
    except subprocess.TimeoutExpired as exc:
        # The teaching kernel deliberately idles forever after self-checks.
        output = exc.output or b""
    output = output.decode("utf-8", errors="replace").replace("\r\n", "\n")
    print(output, end="", flush=True)
    manager_name = "Buddy System" if backend == "buddy" else MANAGERS[backend]
    expected = [f"memory management: {manager_name}",
                "check_alloc_page() succeeded!", "slub_check() succeeded!"]
    if backend == "buddy":
        expected.append("buddy_check() succeeded!")
    for marker in expected:
        if marker not in output:
            raise RuntimeError(f"missing {marker!r}")
    if "kernel panic" in output or "FAIL" in output:
        raise RuntimeError("kernel self-check failed")
    print(f"PASS: {backend} + SLUB, RAM={ram} MiB", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=["all", *MANAGERS], default="all")
    parser.add_argument("--ram", type=int, nargs="+", default=[128])
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    args = parser.parse_args()
    if any(ram <= 0 for ram in args.ram):
        parser.error("RAM values must be positive MiB counts")
    source = Path(__file__).resolve().parents[1]
    backends = list(MANAGERS) if args.backend == "all" else [args.backend]
    for backend in backends:
        with tempfile.TemporaryDirectory(prefix=f"lab2-{backend}-") as directory:
            tree = Path(directory) / "code"
            print(f"=== Temporary verification: {backend} ===", flush=True)
            prepare_tree(source, tree, backend)
            run_build(tree)
            for ram in args.ram:
                run_kernel(tree, backend, ram, args.qemu)
    print("PASS: all selected checks; course framework files were not edited.")


if __name__ == "__main__":
    main()

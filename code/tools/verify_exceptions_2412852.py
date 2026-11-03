#!/usr/bin/env python3
import argparse
import pathlib
import shutil
import subprocess
import tempfile


def context_assembly():
    lines = ['.text', '.option rvc']
    instructions = [('break16', '.2byte 0x9002'),
                    ('break32', '.4byte 0x00100073'),
                    ('illegal16', '.2byte 0x0000'),
                    ('illegal32', '.4byte 0x0000007b')]
    for name, instruction in instructions:
        lines.extend([f'.globl verify_{name}', f'verify_{name}:',
                      'addi sp, sp, -800'])
        for register in range(1, 32):
            lines.append(f'sd x{register}, {register * 8}(sp)')
        for register in range(5, 32):
            lines.append(f'li x{register}, {0x100 + register}')
        for register in range(32):
            lines.append(f'sd x{register}, {256 + register * 8}(sp)')
        lines.extend(['.balign 4', 'c.nop', instruction])
        for register in range(32):
            lines.append(f'sd x{register}, {512 + register * 8}(sp)')
        lines.append('li a0, 0')
        for register in range(32):
            lines.extend([f'ld t0, {256 + register * 8}(sp)',
                          f'ld t1, {512 + register * 8}(sp)',
                          f'bne t0, t1, .Lfail_{name}'])
        lines.extend([f'j .Lrestore_{name}', f'.Lfail_{name}:',
                      'li a0, 1', f'.Lrestore_{name}:'])
        for register in range(1, 32):
            if register not in (2, 10):
                lines.append(f'ld x{register}, {register * 8}(sp)')
        lines.extend(['addi sp, sp, 800', 'ret'])
    return '\n'.join(lines) + '\n'


TEST_SOURCE = r'''
#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>

extern void exception_handler(struct trapframe *tf);
extern uintptr_t verify_break16(void);
extern uintptr_t verify_break32(void);
extern uintptr_t verify_illegal16(void);
extern uintptr_t verify_illegal32(void);

void interrupt_handler(struct trapframe *tf) {
    cprintf("UNEXPECTED INTERRUPT\n");
    while (1) {}
}

void verify_exceptions_2412852(void) {
    unsigned int failures = 0;
    unsigned int checks = 0;
    uint16_t instructions[] = {0x9002, 0x0073, 0x0010, 0x0000, 0x007b, 0x0000};
    unsigned int offsets[] = {0, 1, 3, 4};
    unsigned int lengths[] = {2, 4, 2, 4};
    struct trapframe frame, expected;
    for (unsigned int cause = 2; cause <= 3; cause++) {
        for (unsigned int index = 0; index < 4; index++) {
            memset(&frame, 0x5a, sizeof(frame));
            frame.cause = cause;
            frame.epc = (uintptr_t)&instructions[offsets[index]];
            expected = frame;
            expected.epc += lengths[index];
            exception_handler(&frame);
            failures += memcmp(&frame, &expected, sizeof(frame)) != 0;
            checks++;
        }
    }
    unsigned int causes[] = {0, 1, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 20};
    for (unsigned int index = 0; index < sizeof(causes) / sizeof(causes[0]); index++) {
        memset(&frame, 0, sizeof(frame));
        frame.cause = causes[index];
        expected = frame;
        exception_handler(&frame);
        failures += memcmp(&frame, &expected, sizeof(frame)) != 0;
        checks++;
    }
    cprintf("[2412852] handler cases: %u checks, %u failures\n", checks, failures);
    uintptr_t (*tests[])(void) = {verify_break16, verify_break32,
                                verify_illegal16, verify_illegal32};
    for (unsigned int index = 0; index < 4; index++) {
        uintptr_t result = tests[index]();
        failures += result != 0;
        cprintf("[2412852] real trap %u (32 GPR slots / original stack): %s\n",
                index, result == 0 ? "PASS" : "FAIL");
    }
    cprintf("[2412852] %s (25 cases)\n", failures == 0 ? "PASS" : "FAIL");
    while (1) {}
}
'''


def main():
    parser = argparse.ArgumentParser(description='Independent exception tests in a temporary copy')
    parser.add_argument('--log-dir', type=pathlib.Path,
                        default=pathlib.Path('report/logs'))
    course_qemu = pathlib.Path.home() / 'opt/qemu-4.1.1/bin/qemu-system-riscv64'
    parser.add_argument('--qemu', default=str(course_qemu) if course_qemu.is_file()
                        else 'qemu-system-riscv64')
    arguments = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    arguments.log_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lab3-2412852-') as temporary:
        source = pathlib.Path(temporary) / 'code'
        shutil.copytree(root / 'code', source,
                        ignore=shutil.ignore_patterns('obj', 'bin', 'dev', 'environment', '__pycache__'))
        trap = source / 'kern/trap/trap.c'
        trap.write_text(trap.read_text().replace(
            '//lab3 function2 YOUR CODE',
            '//lab3 function2 YOUR CODE\nvoid interrupt_handler(struct trapframe *tf);'))
        init = source / 'kern/init/init.c'
        init.write_text(init.read_text().replace(
            '    clock_init();',
            '    extern void verify_exceptions_2412852(void);\n'
            '    verify_exceptions_2412852();\n    clock_init();'))
        (source / 'kern/init/verify_2412852.c').write_text(TEST_SOURCE)
        (source / 'kern/init/verify_context_2412852.S').write_text(context_assembly())
        entry = source / 'kern/trap/trapentry.S'
        entry.write_text(entry.read_text().replace('csrr s3, sbadaddr', 'csrr s3, 0x143'))
        versions = subprocess.check_output(['riscv64-unknown-elf-gcc', '--version'])
        versions += subprocess.check_output([arguments.qemu, '--version'])
        versions += b'Temporary copy only: sbadaddr CSR alias replaced with numeric CSR 0x143.\nInterrupt handler is a test stub; timers are not enabled.\n'
        versions += f'QEMU executable: {arguments.qemu}; image loaded with -kernel.\n'.encode()
        build = subprocess.run(['make', '-j2'], cwd=source,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (arguments.log_dir / '2412852-exceptions-build.txt').write_bytes(versions + build.stdout)
        if build.returncode:
            print(build.stdout.decode(errors='replace'))
            raise SystemExit(build.returncode)
        command = [arguments.qemu, '-machine', 'virt', '-nographic', '-bios', 'default',
                   '-kernel', str(source / 'bin/ucore.img')]
        try:
            execution = subprocess.run(command, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=15)
            output = execution.stdout
        except subprocess.TimeoutExpired as error:
            output = error.stdout or b''
        (arguments.log_dir / '2412852-exceptions-qemu.txt').write_bytes(output)
        print(output.decode(errors='replace'))
        if b'[2412852] PASS (25 cases)' not in output:
            raise SystemExit('Exception verification failed; inspect the saved logs')


if __name__ == '__main__':
    main()

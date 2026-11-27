# 操作系统实验 Lab 5 · 用户程序

`lab5` 从 `lab4` 的 `7b67fad` 创建。`code/` 使用 `../lab2026/lab5.zip` 中的课程框架，`report/` 中的现有报告、提示词和截图属于 Lab 4。

## 已接入的前序实现

- Lab 2 的物理内存初始化和 Best-Fit 分配器。分配算法沿用 Lab 4，接入 Lab 5 框架的分配器自检和评分输出。
- Lab 3 的中断分发、异常处理和寄存器恢复。恢复代码按 `sstatus.SPP` 维护返回用户态时所需的 `sscratch`，Lab 3 演示用的定时关机已移除。
- Lab 4 的页映射与引用计数、PCB 初始化、内核线程创建、进程切换和调度。基础 `do_fork` 已通过框架的 `set_links` 接入进程树。当前 `init_main` 和 `copy_mm` 仍是 Lab 4 基础版本。
- 启动时用临时恒等映射完成 Sv39 切换，跳到高地址后清除该映射。三级页表只在叶子项设置用户权限，避免新版 QEMU 拒绝非叶子项的保留位。

## 三人分工

| 成员 | 实现范围 | 验证与报告 |
| --- | --- | --- |
| 2412439 崔迪 | 模块二的启动与装载。在 `proc.c` 负责 `alloc_proc`、`proc_init`、`kernel_thread` 的 Lab 5 适配，以及 `init_main`、`user_main`、`kernel_execve`、`do_execve`、`load_icode`。 | 验证首个用户程序从 ELF 入口执行、BSS 清零和用户栈。覆盖 `hello`、`testbss`，记录镜像嵌入与运行时装载的区别。 |
| 2412819 李子谦 | 模块三以及子进程的退出和回收。在 `proc.c` 负责 `do_fork`、`copy_mm`、`do_wait`、`do_exit`，在 `vmm.c` 和 `pmm.c` 负责 `dup_mmap`、`copy_range`。 | 验证父子返回值、内存隔离、退出码、等待、孤儿接管和失败清理。覆盖 `exit`、`forktest`、`forktree`。 |
| 2412852 梁靖泽 | 模块一和模块四的调度控制。负责 `trap.c` 的 `exception_handler` 及用户态 Trap 收尾、`syscall.c` 的分发与全部 `sys_*` 封装，以及 `proc.c` 的 `do_yield`、`do_kill`。 | 验证调用号与返回值、非法参数、让出和终止。覆盖 `badarg`、`yield`、`spin`、`waitkill`，牵头双重 GDB 跟踪 `ecall`、`sret` 和 QEMU TCG。 |

分工按实现、测试和调试的总工作量划分。若选做 COW，李子谦负责 fork 时的页共享和引用计数，梁靖泽负责写缺页处理，崔迪负责写入隔离测试和状态转换说明。

- `init_main` 由崔迪实现，调用李子谦提供的 `do_wait`。程序映像装载失败或用户进程结束时统一走 `do_exit`。
- 梁靖泽负责系统调用参数分发和 Trap 返回，李子谦负责 `do_wait` 的用户指针检查，崔迪负责 `do_execve` 的用户指针检查。
- 每人记录所负责部分的 spec、prompt、AI 生成代码遇到的 bug 和实际测试结果。补齐后保留 `lab5` 注释，并以负责人学号替换对应的 `YOUR CODE`。三人合并报告并共同运行 `make -C code grade`。

## 接入验证

用 RISC-V GCC 10.2 交叉编译时，正式构建停在待填写的 `sys_*` 处理函数。使用临时测试桩链接后，在 QEMU 11.0.2 上通过内存与页表自检、课程的 Process Create 检查，以及首个内核线程和连续两次用户态 Trap 的往返验证。完整评分等待 Lab 5 各模块实现。

在 QEMU 11.0.2 上，课程 Makefile 的 `-device loader` 启动参数未设置 OpenSBI 的下一跳地址。接入验证使用 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/ucore.img`。

[Lab 5 练习](../lab2026/_book/lab5/lab5_2_1_exercises.html) · [GDB 分支任务](../lab2026/_book/lab5/lab5_4_gdb_ecall.html) · [实验报告要求](../lab2026/_book/lab5/lab5_5_lab_requirement.html)

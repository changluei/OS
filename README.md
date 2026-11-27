# 操作系统实验 Lab 5 · 用户程序

`lab5` 从 `lab4` 的 `7b67fad` 创建。`code/` 使用 `../lab2026/lab5.zip` 中的课程框架。`report/report.md`、`report/prompt.md` 和既有截图记录 Lab 4，Lab 5 各模块的报告见下方。

## 已接入的前序实现

- Lab 2 的物理内存初始化和 Best-Fit 分配器。分配算法沿用 Lab 4，接入 Lab 5 框架的分配器自检和评分输出。
- Lab 3 的中断分发、异常处理和寄存器恢复。恢复代码按 `sstatus.SPP` 维护返回用户态时所需的 `sscratch`，Lab 3 演示用的定时关机已移除。
- Lab 4 的页映射与引用计数、PCB 初始化、内核线程创建、进程切换和调度。基础 `do_fork` 已通过框架的 `set_links` 接入进程树。当前 `copy_mm` 仍是 Lab 4 基础版本。
- 启动时用临时恒等映射完成 Sv39 切换，跳到高地址后清除该映射。三级页表只在叶子项设置用户权限，避免新版 QEMU 拒绝非叶子项的保留位。

## 三人分工

| 成员 | 实现范围 | 验证与报告 |
| --- | --- | --- |
| 2412439 崔迪 | **模块二 · 创建并加载第一个用户进程**。负责 `alloc_proc`、`proc_init`、`kernel_thread` 的适配，以及 `init_main`、`user_main`、`kernel_execve`、`do_execve`、`load_icode` 和 `do_wait`。 | 覆盖 `hello`、`testbss` 和 `waitpid` 回收。负责 **Challenge 2**，说明用户程序何时预加载，以及与常用操作系统加载方式的区别。 |
| 2412819 李子谦 | **模块三 · 复制用户进程**和**Challenge 1 · 写时复制（COW）**。负责 `do_fork`、`copy_mm`、`dup_mmap`、`copy_range` 以及 COW 的完整实现。 | 覆盖 `forktest`、`forktree`，验证父子返回值、内存隔离和失败清理。负责 COW 测试用例及设计报告，说明共享页和私有页的状态转换。 |
| 2412852 梁靖泽 | **模块一 · 用户态系统调用**和**模块四 · 其余系统调用**。负责 `exception_handler`、`syscall` 和全部 `sys_*` 封装，以及 `do_exit`、`do_yield`、`do_kill`。 | 个人验证覆盖系统调用自检、`yield` 和**双重 GDB 分支任务**。模块三接入用户态 `fork` 后验收 `exit`、`badarg`、`spin` 和 `waitkill`。 |

每个模块和 Challenge 由一位成员负责实现、验证和报告。分工按实现、测试和调试的总工作量划分。

- 崔迪和梁靖泽联调 `do_wait` 与 `do_exit` 的退出码、僵尸状态和父进程唤醒。模块二的装载失败路径也调用 `do_exit`。
- 每人记录所负责部分的 spec、prompt、AI 生成代码遇到的 bug 和实际测试结果。补齐后保留 `lab5` 注释，并以负责人学号替换对应的 `YOUR CODE`。各模块和报告合并后，小组统一运行 `make -C code grade`。

## 接入验证

模块二已实现首个用户进程装载、进入 U 态和等待回收。使用临时测试桩通过 `hello`、`testbss`、指定 PID 等待、ELF 部分装载失败后回收以及从旧用户空间替换程序映像的测试。Challenge 2 已记录在同一份阶段报告中。 [模块二报告](report/lab5-module2.md)

模块一和模块四已接入系统调用、退出回收、主动让出和终止请求。当前分支通过 `Syscall`、`Process Control` 自检和 `yield`。双重 GDB 实测了 `getpid` 从 U 态进入 S 态再返回的过程。 [模块一、模块四与双重 GDB 报告](report/lab5-2412852.md) · [编码提示词](report/lab5-2412852-prompt.md)

在 QEMU 7.0.0 和 11.0.2 上，课程 Makefile 的 `-device loader` 启动参数未设置 OpenSBI 的下一跳地址。单项验证使用 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/ucore.img`。

[Lab 5 练习](../lab2026/_book/lab5/lab5_2_1_exercises.html) · [GDB 分支任务](../lab2026/_book/lab5/lab5_4_gdb_ecall.html) · [实验报告要求](../lab2026/_book/lab5/lab5_5_lab_requirement.html)

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
| 2412439 崔迪 | **模块二 · 创建并加载第一个用户进程**。负责 `alloc_proc`、`proc_init`、`kernel_thread` 的适配，以及 `init_main`、`user_main`、`kernel_execve`、`do_execve`、`load_icode` 和 `do_wait`。 | 覆盖 `hello`、`testbss` 和 `waitpid` 回收。负责 **Challenge 2**，说明用户程序何时预加载，以及与常用操作系统加载方式的区别。 |
| 2412819 李子谦 | **模块三 · 复制用户进程**。负责 `do_fork`、`copy_mm`、`dup_mmap` 和 `copy_range`。 | 覆盖 `forktest`、`forktree`，验证父子返回值、内存隔离和失败清理。若选做 **Challenge 1**，由李子谦负责整项 COW 实现、测试和设计报告。 |
| 2412852 梁靖泽 | **模块一 · 用户态系统调用**和**模块四 · 其余系统调用**。负责 `exception_handler`、`syscall` 和全部 `sys_*` 封装，以及 `do_exit`、`do_yield`、`do_kill`。 | 覆盖 `exit`、`badarg`、`yield`、`spin`、`waitkill`。负责**双重 GDB 分支任务**，跟踪 `ecall`、`sret` 和 QEMU TCG。 |

每个模块和 Challenge 由一位成员负责实现、验证和报告。分工按实现、测试和调试的总工作量划分。

- 崔迪和梁靖泽联调 `do_wait` 与 `do_exit` 的退出码、僵尸状态和父进程唤醒。模块二的装载失败路径也调用 `do_exit`。
- 每人记录所负责部分的 spec、prompt、AI 生成代码遇到的 bug 和实际测试结果。补齐后保留 `lab5` 注释，并以负责人学号替换对应的 `YOUR CODE`。三人合并报告并共同运行 `make -C code grade`。

## 接入验证

用 RISC-V GCC 10.2 交叉编译时，正式构建停在待填写的 `sys_*` 处理函数。使用临时测试桩链接后，在 QEMU 11.0.2 上通过内存与页表自检、课程的 Process Create 检查，以及首个内核线程和连续两次用户态 Trap 的往返验证。完整评分等待 Lab 5 各模块实现。

在 QEMU 11.0.2 上，课程 Makefile 的 `-device loader` 启动参数未设置 OpenSBI 的下一跳地址。接入验证使用 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/ucore.img`。

[Lab 5 练习](../lab2026/_book/lab5/lab5_2_1_exercises.html) · [GDB 分支任务](../lab2026/_book/lab5/lab5_4_gdb_ecall.html) · [实验报告要求](../lab2026/_book/lab5/lab5_5_lab_requirement.html)

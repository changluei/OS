# 操作系统实验仓库：Lab 3 中断与异常

当前分支为 `lab3`，从 `lab2` 的 `0a7f27b` 创建。`code/` 以同级目录 `../lab3_new/` 的 56 个课程文件为基线，已迁入 Lab 2 的内存初始化和 Best-Fit 实现；除这两个内存文件外，其余 54 个课程文件仍与基线逐字节一致。Lab 3 的 Trap 填空尚未实现。Lab 2 的完成代码和报告保留在 `lab2` 分支。

本分支现有 `report/` 仍为继承的 Lab 2 报告及附件，不能作为 Lab 3 的实验结果。后续完成实验时再整理 Lab 3 报告。

## 依据与实验主线

本地指导书位于 `../lab2026/_book/lab3/`，重点阅读：

- [练习与实现边界](../lab2026/_book/lab3/lab3_2_1_exercise.html)
- [项目组成和执行流](../lab2026/_book/lab3/lab3_2_2_files.html)
- [Trap 入口与上下文](../lab2026/_book/lab3/lab3_3_2_trapentry.html)
- [时钟中断](../lab2026/_book/lab3/lab3_3_4_clock.html)
- [报告与提交要求](../lab2026/_book/lab3/lab3_4_lab_requirement.html)

这些指导书链接依赖当前本地目录布局，不属于仓库内文件。实际接口和测试以 `code/` 的课程框架为准。

实验目标是形成完整闭环：设置 `stvec` 与时钟 → 发生 Trap → `SAVE_ALL` 保存现场 → `__alltraps` 调用 C 函数 `trap(tf)` → 分发中断或异常 → `RESTORE_ALL` 恢复现场 → `sret` 返回原执行流。本实验主要验证内核 S 态中的断点、非法指令和时钟处理；不能把 `sret` 一律理解为返回 U 态，应按保存的 `SPP` 返回。

## 待办（按依赖顺序）

- [x] 从 Lab 2 创建并切换到 `lab3` 分支。
- [x] 将 `code/` 替换为 `lab3_new` 原始代码，移除 Lab 2 特有源码、测试工具与旧构建产物；保留本地 `code/dev`、`code/environment/`。
- [x] 阅读 Lab 3 指导书、源码填空位置和课程评分脚本，整理任务与差异。

### 1. 补齐 Lab 2 前置依赖

- [x] 从 `lab2` 分支迁入 `kern/mm/pmm.c` 的 `page_init()`，保留 `2412439` 署名及 Lab 3 的 `local_intr_save/restore` 包装和调用流程。Lab 3 不再定义 Lab 2 的物理边界宏，因此在填空区域内用 `KERNTOP/KERNBASE - PHYSICAL_MEMORY_OFFSET` 计算相应物理边界。
- [x] 从 `lab2` 分支迁入 `kern/mm/best_fit_pmm.c` 的分配器实现及管理器定义，保留 `2412819` 署名；课程原有 `basic_check()`、`best_fit_check()` 逐字节不变。管理器直接调用 `best_fit_check()`，不再依赖 Lab 2 的 SLUB 组合自检。
- [x] 核对并保留 Lab 3 框架默认的 `default_pmm_manager`（First-Fit）。Best-Fit 实现及其管理器已迁入，但未切换为默认分配器。

### 2. 功能模块一：Trap 上下文恢复与入口

- [ ] 为本模块编写 spec 和提示词，明确输入、栈布局、调用约定、恢复顺序及不可修改的接口。
- [ ] 在 `kern/trap/trapentry.S` 的第一个 `lab3 function1` 标记处实现 `RESTORE_ALL`，与 `SAVE_ALL` 的 36 个寄存器槽位对应；恢复通用寄存器、`sstatus`、`sepc`，最后恢复原 `sp`。
- [ ] 在第二个 `lab3 function1` 标记处补全 `__alltraps`：按 RISC-V 调用约定将当前 Trap frame 地址传给 `trap(struct trapframe *tf)`，返回时保证栈指针仍指向当前 Trap frame，随后经 `__trapret` 和 `sret` 返回。
- [ ] 保持 `SAVE_ALL`、`struct trapframe`、`trap()` 签名及四字节对齐约束不变。

### 3. 功能模块二：中断与异常处理

- [ ] 为本模块编写 spec 和提示词，列出中断分类、输出格式、EPC 更新规则、计数与关机条件。
- [ ] 在 `kern/trap/trap.c` 的 `lab3 function2` 区域实现 `interrupt_handler()`：识别中断原因；S 态时钟中断每次调用 `clock_set_next_event()`，并将 `ticks` 加一，每满 `TICK_NUM=100` 次调用现有 `print_ticks()`。
- [ ] 处理其他中断分类并输出相应提示；未知中断输出原因并调用 `print_trapframe(tf)`。课程测试要求能看到 `Supervisor software interrupt`、`Unknown interrupt: cause 15`。
- [ ] 实现 `exception_handler()`：非法指令输出含 `Illegal instruction at` 的信息，断点输出含 `Breakpoint at` 的信息；按指令实际长度更新 `tf->epc`，保证返回后继续执行。不能把所有异常统一加 4；需兼顾压缩断点和普通断点。其他异常调用 `print_trapframe(tf)` 兜底。
- [ ] 核实并补上指导书要求的 `SHUTDOWN_AFTER` 关机策略。原文在指导书 `lab3_2_1_exercise.html` 第 2160 行的“功能模块二”注意事项中：“累计输出达到 `SHUTDOWN_AFTER` 后需要调用框架提供的关机接口。”第 2162 行也将其列为依赖宏。当前骨架没有定义这个宏，指导书也未给出具体阈值；实现前需要确认阈值，在允许的填空区域补齐必要定义/声明，并通过框架的 `sbi_shutdown()` 关机。
- [ ] 保留 `trap.c` 其他既有函数和评分框架，时钟中断返回时保持被打断位置的 EPC。

### 4. 验证与留存证据

- [ ] 准备 RISC-V 工具链及课程 QEMU 环境，执行 `make clean && make`，确认初始化与新增代码编译通过。
- [ ] 执行 `make qemu`，确认内存检查完成、时钟持续触发并重复输出 `100 ticks`，另外验证规定次数后的自动关机。
- [ ] 执行 `make grade`，目标为三项全部通过、`Total Score: 100/100`：

  | 课程评分项 | 分值 | 主要检查 |
  | --- | ---: | --- |
  | Trap 入口、寄存器恢复及断点 EPC | 35 | 断点返回后寄存器现场可用 |
  | 非法指令 EPC 及兜底处理 | 25 | 跳过 4 字节非法指令，软件/未知中断和未知异常输出 |
  | 持续 S 态时钟中断 | 40 | 重设定时器、`100 ticks`、累计到 `ticks=200` |

- [ ] 补充评分未覆盖的必要检查：16/32 位断点长度、原栈与中断状态恢复、关机阈值；正常运行测试与评分结果分别记录。
- [ ] 保存真实构建、运行、评分日志及截图，记录实际遇到的问题和修复，不把待办写成已经完成的结果。

### 5. 报告与提交

- [ ] 整理本分支 `report/`，按课程 Markdown 报告模板编写 Lab 3 报告，核实成员与分工。
- [ ] 说明整体实现顺序、核心函数、Trap frame 布局与上下文保存/恢复的关系。
- [ ] 记录两个模块的 spec、最终 prompt、真实迭代过程、bug 与修复，并整理相应 `report/prompt.md`。
- [ ] 总结实验知识点与 OS 原理的联系和差异，以及本实验未覆盖的重要原理知识点。
- [ ] 保留课程 `lab3` 注释，将 `YOUR CODE` 替换为实际负责人的学号；核对 Lab 2 继承部分的署名。
- [ ] 完成代码审查、报告与附件核对，提交并推送 `lab3` 分支。

## 框架差异与环境状态

指导书练习开头提到填写“lab2”位置，但实际 Lab 3 编程任务是 `trapentry.S` 的两个 `lab3 function1` 标记和 `trap.c` 的一个 `lab3 function2` 标记；另有两个 `lab2` 标记用于迁入前置内存代码。指导书示例目录和注释存在旧版本内容，应结合当前源码核对，Challenge 不计入上述必做清单。

评分入口是 `make grade`：在 `/tmp/lab3_grade` 临时副本中注入测试，再执行 `tools/grade.sh`，不应直接向正式源码手工注入测试。评分脚本未检查自动关机，100/100 仍需配合正常运行检查才能覆盖指导书要求。

已完成基线切换、任务梳理及 Lab 2 前置实现迁入。迁移后在临时宿主机测试中，`page_init()` 的 12 个边界用例通过；Best-Fit 使用真实链表实现及迁入的分配器，通过原有 `basic_check()`、`best_fit_check()`，256 页全部回收。测试对物理地址和原子位操作使用宿主机替代，不验证 RISC-V 指令或时钟行为。

尚未完成 RISC-V 构建或课程评分。Lab 3 的两个 handler 和上下文恢复仍留空；本机 C 编译器不支持课程 RISC-V 内联汇编约束，不能用于完整源码编译。检查时 Docker 服务未启动，本地 `riscv64-unknown-elf-gcc` 也不在 PATH 中。

本地 `code/dev` 和 `code/environment/` 继续由 `.git/info/exclude` 忽略；其中旧 `verify` 流程面向 Lab 1，不是 Lab 3 的验收入口。切换前的代码、构建产物及 README 另行备份于同级 `.os-labs-lab3-backup-20261028-225324/`。

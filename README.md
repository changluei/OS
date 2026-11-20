# 操作系统实验仓库：Lab 4 进程管理

当前分支为 `lab4`，从 `lab3` 的 `a8e2b7c` 创建。Lab 4 的三人分工已确定，详细任务、函数边界、协作顺序和验收要求见 [Lab 4 分工方案](report/lab4-division.md)。

当前 `code/` 以用户提供的 `../lab4.zip` 中 `lab4_new/` 为基线，已导入全部 68 个文件，并迁入 Lab 2/3 的物理页初始化、Best-Fit 分配器和 Trap 处理。Lab 4 三个新模块的填空保留，继续按下方分工完成。来源、迁移边界和检查结果见 [框架接入记录](report/lab4-framework.md)。

## 三人分工

| 成员 | 主要任务 | 配套工作 |
|------|----------|----------|
| 2412439-崔迪 | `page_insert()`、`page_remove()`、`alloc_proc()`、`proc_init()` | 页映射与引用计数、PCB 分配、idle/init 初始化；本人代码的验证和文档 |
| 2412819-李子谦 | `kernel_thread()`、`do_fork()`、`copy_thread()`、`copy_mm()` | 线程创建、内核栈与 PID、错误清理、首次运行现场；本人代码的验证和文档 |
| 2412852-梁靖泽 | `cpu_idle()`、`schedule()`、`proc_run()`、`switch_to()` | 调度策略、临界区、Sv39 页表切换和寄存器上下文；本人代码的验证和文档 |

按代码量、实现难点和验证范围综合分配，每人负责四个必做函数。`proc.c` 中崔迪负责 `alloc_proc()`、`proc_init()`，李子谦负责四个线程创建函数，梁靖泽负责 `cpu_idle()`、`proc_run()`。整体联调、课程验收与报告公共部分由三人共同完成。Challenge 1、2 均为文字思考题，没有新增代码要求，暂不指定负责人，也不计入本轮工作量。具体共享接口约定见分工方案。

## 进度

- [x] 创建并切换到 `lab4` 分支。
- [x] 核对三人的姓名、学号，完成十二个必做函数及验证、文档职责分工。
- [x] 导入 Lab 4 课程框架，迁移 Lab 2/3 的必要实现。
- [ ] 完成模块一：虚拟内存页操作。
- [ ] 完成模块二：进程创建与初始化。
- [x] 完成模块三：进程调度与上下文切换（2412852，2026-11-20）。
- [ ] 三人分别完成各自代码的规格、提示词和调试记录。
- [ ] 整理 Challenge 思考题；当前暂不分配负责人，不计入编程工作量。
- [ ] 完成 Lab 4 整体构建、运行、课程验收与正式报告。

梁靖泽已完成本人四个函数；两份 Lab 0.5 四段式提示词由主 agent 编写并分别发送给代码子 agent，代码完成后由主 agent 补齐本人报告与验证记录。详见 [提示词](report/prompt.md)、[模块三报告](report/report.md#功能模块三调度与上下文切换)及 [真实验证日志](report/logs/2412852-lab4-tests.txt)。2026-11-20 使用命令行 GCC 兼容选项和 QEMU 4.1.1 完成内核构建、idle/init 首次运行及课程评分 **30/30**；评分注入只在临时副本进行，未修改课程工具或队友代码。其他成员文档、Challenge、截图和正式报告公共部分仍待小组整理，不能视为全部交付完成。

## 实验依据与材料

- [课程 Lab 4](http://oslab.mobisys.top/lab2026/_book/lab4/lab4.html)、[练习](http://oslab.mobisys.top/lab2026/_book/lab4/lab4_2_1_exercises.html)、[报告要求](http://oslab.mobisys.top/lab2026/_book/lab4/lab4_5_labs_requirement.html)。本地同步副本为同级 `../lab2026/_book/lab4/`，本次分工依据 2026-11-11 下载的内容。
- [Lab 4 分工方案](report/lab4-division.md)：本阶段的任务与交付约定。
- [Lab 4 框架接入记录](report/lab4-framework.md)：压缩包来源、前置代码迁移、框架差异及构建结果。
- [Lab 3 仓库说明](report/archive/lab3-readme.md)：前置代码、开发环境和历史验收说明。
- [现有实验报告](report/report.md)、[现有提示词](report/prompt.md)、`report/logs/` 和 `report/images/`：报告和提示词已有 Lab 4 成员章节，日志同时保留历史检查和本次验证，截图仍待小组整理。

网站练习页已提示：`libs/riscv.h` 中的旧 `lsatp()` 是 Sv32 遗留实现，不能用于本实验的 Sv39 页表切换。模块三应参考 Lab 4 框架 `kern/mm/pmm.c` 的 `enable_paging()`，正确设置 `satp` 并刷新 TLB。

2026-11-11 已在现有 Docker 环境执行构建：源码编译与汇编完成，链接因 Lab 4 尚未实现的 `page_insert()`、`page_remove()`、`proc_init()`、`cpu_idle()` 而失败。详见 [构建日志](report/logs/20261111-lab4-framework-build.txt)；当时尚未生成可运行内核，运行与课程评分待三个模块完成后执行。

以上为 2026-11-11 接入时的历史状态；本次可运行镜像及 30/30 验收结果以 2026-11-20 的验证日志为准。正式 init 返回后仍进入框架 `do_exit()` 的占位 panic，本次未扩展进程退出功能。

# Lab 4 框架接入与前置代码迁移记录

日期：2026-11-11。分支：`lab4`。本次接入由当前协作会话完成；三人的任务主责见 [分工方案](lab4-division.md)，迁入代码的原有署名保持不变。

## 框架来源

- 用户提供的压缩包：仓库同级 `../lab4.zip`。
- 压缩包内根目录：`lab4_new/`，接入位置：仓库 `code/`。
- 源文件数量：68，总大小：210811 字节。
- 压缩包 SHA-256：`7a92f243f88ca9a4ec78b52e554a14d78a41bbf6a79a1275f6cc929aaebceba2`。
- 前置实现来源：本仓库 `lab3` 分支的 `a8e2b7c`，其中包含此前迁入的 Lab 2 实现。

先完整导入新框架，再在前置实验标记处迁移已有实现。`code/dev` 和 `code/environment/` 的本地环境配置保留。旧 Lab 3 的独立评分文件和被新框架替换的库文件已移出当前代码树，历史版本可从 `lab3` 分支取得。

## 前置迁移范围

| 当前文件 | 迁入内容 | 原实现署名及适配说明 |
|----------|----------|--------------------|
| `code/kern/mm/pmm.c` | `page_init()` | 2412439；沿用 DTB 内存范围检查、物理页描述符初始化与可用页登记，使用 Lab 4 的结构定义和 `sizeof(struct Page)` |
| `code/kern/mm/default_pmm.c` | Best-Fit 的空闲链表、初始化、分配、释放、合并和空闲页统计 | 2412819；取自原 `best_fit_pmm.c`，通过框架要求的 `default_pmm_manager` 对外提供，管理器名称保留 `best_fit_pmm_manager` |
| `code/kern/trap/trapentry.S` | `RESTORE_ALL`、向 C 层传递 trapframe 的入口代码 | 2412439；保留 Lab 4 的 `SAVE_ALL`、返回流程及新增 `forkrets` |
| `code/kern/trap/trap.c` | 中断分类、时钟处理、异常处理和指令判长 | 中断部分 2412819，异常部分 2412852；保留 Lab 4 自带的 `idt_init()`、`SSTATUS_SUM` 设置和分发流程 |

分配器有一项必要的框架适配：压缩包 `default_pmm.c` 引用了未附带的 `best_fit_pmm.h`，其自检又使用 `best_fit_check()`。因此将头文件引用修正为现有 `default_pmm.h`，在 Lab 2 填空处迁入本组已有 Best-Fit 算法，并定义框架所需的 `default_pmm_manager`。压缩包中的 `basic_check()` 和 `best_fit_check()` 正文完整保留。

迁入的定时中断仍保留 Lab 3 的行为：每 100 次时钟中断打印一次，默认打印 10 次后通过 SBI 关机。

## 保留给 Lab 4 的填空

| 文件中的原始标记 | 对应任务 | 负责人 |
|------------------|----------|--------|
| `kern/mm/pmm.c`：`LAB4 function1 YOUR CODE` | `page_insert()`、`page_remove()` | 崔迪，2412439 |
| `kern/process/proc.c`：`LAB4 function2 YOUR CODE` | 六个线程创建与初始化函数 | 李子谦，2412819 |
| `kern/process/proc.c`：`LAB4 function3 YOUR CODE` | `cpu_idle()`、`proc_run()` | 梁靖泽，2412852 |
| `kern/schedule/sched.c`：`LAB4 function3 YOUR CODE` | `schedule()` | 梁靖泽，2412852 |
| `kern/process/switch.S`：`LAB4 function3 YOUR CODE` | `switch_to()` | 梁靖泽，2412852 |

以上五处原始标记均保留。`proc_struct`、`context`、VMA、`kmalloc`、`kernel_thread_entry` 和课程评分工具使用压缩包版本。旧 `lsatp()` 的 Sv32 问题仍需由模块三按指导书要求处理。

## 检查结果

逐文件核对压缩包后，只有上表四个前置迁移文件发生变化，其余 64 个框架文件与压缩包逐字节一致。`tools/` 下全部课程工具、Makefile、结构定义和进程/调度框架均保持原样；分配器的两个课程自检函数也与压缩包一致。

在现有容器 `os-labs-lab-1` 中执行：

```bash
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make clean && make'
```

源码编译与汇编完成，链接失败，`make` 返回码为 2。链接器报告尚未定义的函数为 `proc_init()`、`cpu_idle()`、`page_insert()`、`page_remove()`，均属于尚待完成的 Lab 4 填空。完整输出见 [本次构建日志](logs/20261111-lab4-framework-build.txt)。

这次检查确认了前置代码可以在新框架下编译，以及当前链接失败的位置；尚未生成可运行内核，未执行 Lab 4 运行验证或课程评分。

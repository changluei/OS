# Lab 3 提示词记录

## 2412819—李子谦

### 中断分类、时钟与关机

````text
[PROMPT]
任务：我负责 Lab3 的中断处理，请帮我补全 code/kern/trap/trap.c 中的 interrupt_handler()。
操作要求：直接修改实际文件，只动 lab3 function2 填空里的中断部分，署名 2412819。队友写好的异常处理、汇编和其他函数都别改，也不要新增文件、改评分工具或提交 Git。
输出要求：按下面的接口和规格实现，简单够用就行。最后说明改了哪里，有问题就如实告诉我；这一步先不运行测试。

[RELY]
先看 trap.h、riscv.h、clock.h 和 sbi.h 的真实定义。trapframe 含 gpr、status、epc、badvaddr、cause，后四项都是 uintptr_t。
已有 TICK_NUM=100、volatile size_t ticks，以及 clock_set_next_event()、print_ticks()、print_trapframe(tf)、sbi_shutdown()。IRQ 类型使用 riscv.h 的宏；cause 最高位是中断标志，其余位是原因编号。
手册没给关机阈值，先把 SHUTDOWN_AFTER 暂定为 10，表示输出次数，不是 tick 数；注明这是暂定值，并允许编译时覆盖。

[GUARANTEE]
void interrupt_handler(struct trapframe *tf);
只补这个函数和必要的宏、声明。保留共享 lab3 function2 标记，去掉 YOUR CODE，并保留队友的 2412852 署名。

[SPECIFICATION]
interrupt_handler：
Pre-Condition：单核串行调用，tf 是有效且可写的现场；初始化已完成，ticks 从零开始。
Post-Condition：根据原因处理中断，不改 tf 的任何字段，尤其不能增加 epc；除 S 态时钟外不改变 ticks。
Case 1：S 态时钟每次都重设下一次事件，ticks 加一；每满 100 次调用已有 print_ticks()，输出累计达到 SHUTDOWN_AFTER 次后调用真实 sbi_shutdown()。
Case 2：其他已定义 IRQ 只输出相应类型提示，包括 Supervisor software interrupt，不做本实验没要求的设备驱动。
Case 3：未知编号输出 Unknown interrupt: cause N，再调用 print_trapframe(tf)。去掉中断标志后分类，也兼容测试直接传入不带标志的编号。
Requirements：默认第 1000 次时钟中断才关机，第 200 次必须能正常返回；不要为通过评分关闭关机功能，也不要加无关的防御代码。
````

## 其他成员已有记录

### 一、记录边界

本轮用户原话：

> 阅读REAMDE.md完成其中梁靖泽的任务

实际读取 `README.md`。以下是执行过程中按分工整理的规格，不声称用户曾逐字发送这些事前提示词。未使用子代理，未虚构对话。Lab 2 提示词归档在 `archive/lab2-prompt.md`。

### 二、异常模块 spec / 执行约束（梁靖泽整理）

#### RELY

- `trap_dispatch()` 已按 cause 分流，只补 `lab3 function2` 中本人异常逻辑。
- 原 frame 含 GPR、status、epc、badvaddr、cause；汇编将修改后 epc 恢复至 sepc。
- 目标为可读的内核代码、16/32 位指令；EPC 可以仅两字节对齐。
- 使用框架 CAUSE 宏、cprintf 和 print_trapframe，不改公共接口。

#### SPEC

- 非法指令输出含 `Illegal instruction at`，断点输出含 `Breakpoint at`，包含原 EPC。
- 首半字低两位非 `11` 则 EPC+2，否则+4，两分支共用辅助逻辑。
- 不修改其他 frame 字段、不直接写返回 CSR，不改变统一返回路径。
- 其他异常只打印 frame，不取指、不修改 EPC。
- 保留共享标记，本人实现署 `lab3 2412852`；不改中断、模块一和课程工具。

#### 验证要求

在临时副本覆盖两种长度、半字边界、未知异常零 EPC、真实 Trap 的 GPR/栈恢复。中断替身隔离他人待实现模块，不作为中断完成证据。保存原始构建/运行日志，独立验证不冒充总评分。

### 三、模块一资料整理

实现者为崔迪，来源为原 `trapentry.S`、README 及提交 `f9fd7b3`。未取得原始 AI 对话，不能提供其“真实事前提示词”。可核对规格为：匹配 36 槽、恢复 sstatus/sepc、最后恢复 sp、a0 传 frame、保持对齐并走原 sret。梁靖泽整理不计为本人实现，既有验证与本轮再验证分开说明。

### 四、真实迭代

1. 阅读分工、指导书、源码及课程评分，不代写中断，不实施 Challenge。
2. 实现异常与独立工具。
3. 首次构建遭遇旧 CSR 别名不兼容，仅测试副本改为数值 `0x143`。
4. QEMU 默认固件下一跳为零，测试启动改用 `-kernel`，不改正式 Makefile。
5. QEMU 7.0.0 改用 -kernel 后仍无内核输出，保留课程启动源码，切换已安装的课程 QEMU 4.1.1，25/25 用例通过；工具默认优先选择该课程版本。
6. 整理原始日志、报告和原理题；未发生的调试过程不补写。

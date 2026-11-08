# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 3：中断与异常 |
| **小组成员** | 2412439-崔迪、2412819-李子谦、2412852-梁靖泽 |
| **完成日期** | 2026-11-08 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412439-崔迪 | `RESTORE_ALL`、`__alltraps` 和现场恢复验证 |
| 2412819-李子谦 | `interrupt_handler()`、时钟计数与关机、中断保护思考题、整体验证 |
| 2412852-梁靖泽 | `exception_handler()`、指令长度判断、异常与现场恢复联合验证、`stvec` 思考题；整理模块一说明、原理总结及报告附件 |

三人分别撰写负责模块的实现说明和测试结果，梁靖泽汇总报告，组长整理测试截图。

---

## 一、实验目的

1. 理解 RISC-V 中断与异常的区别，以及 Trap 现场保存、处理和返回的过程。
2. 实现中断分类、持续时钟中断、计数输出和自动关机。
3. 理解保存、恢复原中断状态对临界区的作用。
4. 根据 RISC-V 的 16/32 位指令编码处理异常返回位置，验证异常处理前后的通用寄存器和原栈保持一致。
5. 学习按 Lab 0.5 的四段式要求编写提示词，并结合代码和测试检查生成结果。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2412439-崔迪 | Codex 桌面应用；Claude Code | deepseek-v4-flash | |
| 2412819-李子谦 | Claude Code | deepseek-v4-flash | |
| 2412852-梁靖泽 | Codex CLI | deepseek-v4-flash | |

成员开发环境

| 成员 | 宿主机与 Linux 环境 | RISC-V GCC / binutils | QEMU / GDB |
|------|---------------------|----------------------|------------|
| 崔迪 | macOS ARM64；Docker Ubuntu 22.04 / aarch64 | 10.2.0 / 2.35.1 | 4.1.1 / gdb-multiarch 12.1 |
| 李子谦 | Windows WSL2；Ubuntu 24.04.4 LTS / x86_64 | 10.2.0 / 2.35 | 4.1.1 / riscv64-unknown-elf-gdb 10.1 |
| 梁靖泽 | Windows WSL2；Ubuntu 22.04 / x86_64 | 15.1.0 / 2.45 | 4.1.1 / riscv64-unknown-elf-gdb 16.3.90.20250610-git |

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章要解决的是：发生中断或异常后，内核怎样处理事件，再回到原来的程序。Lab 2 已经提供内存和内核栈，但仅有这些还不够；处理器发生 Trap 时不会自动把所有通用寄存器保存到内存。如果入口直接调用 C，处理函数使用的寄存器就可能覆盖被打断程序的值。因此，本章先解决保存与恢复，再建立汇编到 C 的调用关系，最后实现具体事件的处理。

硬件记录 `sepc/scause/stval` 并更新状态位，入口汇编把通用寄存器和返回状态保存为 `trapframe`，C 函数根据原因处理事件，最后由汇编恢复现场并返回。异常处理位于这条流程的中间：它不重新构造现场，而是只修改保存的返回位置，再交给已有的恢复代码。

### 3.2 功能的逐步实现

1. **准备入口和事件。** `idt_init()` 将 `stvec` 设置为 `__alltraps`；`clock_init()` 安排第一次定时事件，`intr_enable()` 开启 S 态中断响应。
2. **形成软件现场。** 发生 Trap 后，硬件保存原因和返回位置，`SAVE_ALL` 在当前内核栈建立 frame，入口把它作为参数交给 `trap(tf)`。
3. **根据原因处理。** `trap_dispatch()` 用 cause 最高位分流。时钟分支重新设置事件并计数；异常分支对非法指令和断点输出诊断，并决定下一条指令的位置。
4. **按保存的状态返回。** C 处理函数返回后，`RESTORE_ALL` 恢复寄存器和返回 CSR，`sret` 完成控制流转移。本实验主要是 S 态内核被打断后回到 S 态，没有额外实现用户态切换。

两类处理对 EPC 的要求不同：异步中断不能使原程序少执行一条指令；本练习的断点和非法指令则必须跳过，否则返回后会再次触发同一个异常。

---

## 四、实验内容与实现

本实验依赖的 Lab 2 代码已填入 `pmm.c`、`best_fit_pmm.c` 对应位置，保留原有署名。下面按手册顺序说明 Lab 3 的两个功能模块，其中模块二按中断和异常分工分别介绍。

### 4.1 功能模块一：Trap 上下文恢复与入口

**负责人：** 2412439-崔迪

#### 模块功能描述

**修改文件：** `code/kern/trap/trapentry.S`

**需要实现/修改的汇编功能：**

```asm
.macro RESTORE_ALL
    # 恢复 SAVE_ALL 保存的现场
.endm

__alltraps:
    # 保存现场并调用 trap(struct trapframe *tf)
```

**功能说明：**

RV64 的 Trap frame 包含 36 个槽，共 288 字节；原栈按 16 字节对齐时，分配 frame 后仍保持对齐。`RESTORE_ALL` 必须与已有 `SAVE_ALL` 的布局对应。

代码从第 32、33 槽恢复 `sstatus/sepc`，然后恢复 `x1`、`x3–x31`，最后从第 2 槽恢复原 `sp`。如果提前恢复栈指针，后续就无法继续按当前 frame 的位置读取寄存器。`x0` 恒为零，不需要恢复；`badvaddr/cause` 用于诊断，不回写对应 CSR。

`__alltraps` 通过 `mv a0, sp` 将 frame 地址传给 `trap()`，返回后进入 `__trapret`，恢复现场并执行 `sret`。返回特权级由保存的 SPP 决定，不能认为 `sret` 总是返回用户态。

编写规格时，我们重点区分了当前 frame 的基址和 Trap 前的栈指针，并按实际保存布局列出恢复要求：

| 槽位 | 保存内容 | 恢复要求 |
|------|----------|----------|
| 0～31 | x0～x31，含第 2 槽中的原 sp | 恢复可写 GPR，x0 恒零；恢复 sp 前必须完成对 frame 的其他访问 |
| 32、33 | sstatus、sepc | 使用 frame 中的返回状态，包含 C 处理函数对 epc 的修改 |
| 34、35 | badvaddr、cause | 用于诊断，不要求回写 CSR |

提示词的 `[RELY]` 给出槽位和调用约定，`[GUARANTEE]` 列出两处汇编功能。规格要求入口把同一个 frame 交给 C，返回时采用 frame 中的状态，尤其要保留异常函数对 EPC 的修改。

#### 最终提示词

以下为按四段式整理的模块提示词，与 [prompt.md](prompt.md) 中对应内容一致。

````text
[PROMPT]
任务：我负责 Lab 3 功能模块一。请补全 code/kern/trap/trapentry.S 中的 RESTORE_ALL 和 __alltraps，使内核能够将保存的 Trap 现场交给 C 函数处理，并恢复现场、返回被打断的 S 态执行流。
操作要求：必须直接修改工程中的实际文件，只补充两处 lab3 function1 YOUR CODE。保留 lab3 function1 注释，将 YOUR CODE 替换为我的学号 2412439。SAVE_ALL、__trapret、sret、入口对齐、trapframe 定义、头文件、Makefile、trap.c 和课程测试均保持原样，不实现已经取消的用户态切换 Challenge。
输出要求：依据下面的真实布局、接口与行为规格实现，说明实际修改位置、栈与返回状态的一致性，以及已执行的验证。模块二尚未实现时，可以在临时副本中使用测试替身验证模块一，但不能把替身写回正式源码，也不能据此声称完整课程评分通过。

[RELY]
code/libs/riscv.h 已定义 LOAD、STORE、REGBYTES。RV64 下 LOAD 为 ld、STORE 为 sd、REGBYTES 为 8。
code/kern/trap/trap.h 中的相关接口为：
```c
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
void trap(struct trapframe *tf);
```
struct pushregs 的成员按 x0 到 x31 顺序排列，RV64 下每项 uintptr_t 占 8 字节。Trap frame 共 36 个槽、288 字节，偏移以 REGBYTES 为单位：第 0～31 槽对应 x0～x31，第 32～35 槽分别为 sstatus、sepc、badvaddr 和 scause。
现有 SAVE_ALL 已在当前内核栈分配 frame，并保存上述现场。当前 sp 指向 frame 基址；第 2 槽保存进入 Trap 之前的原 sp，其值来自 sscratch，不是减去 frame 大小之后的 sp。x0 恒为零。badvaddr/cause 是诊断信息，不是需要恢复的返回控制状态。
RISC-V 调用约定以 a0 传递第一个参数，函数调用会改变返回地址和部分通用寄存器。框架已经提供 __trapret：执行 RESTORE_ALL 后执行 sret。处理函数可通过 tf->epc 调整返回地址；sret 根据恢复的 sepc 和 sstatus 完成返回，不能认为它必然返回 U 态。

[GUARANTEE]
必须完成的汇编接口：
```asm
.macro RESTORE_ALL
    #lab3 function1 2412439
.endm

__alltraps:
    SAVE_ALL
    #lab3 function1 2412439
```
RESTORE_ALL 是无参数汇编宏，__alltraps 是框架已有的汇编入口标签，不将它们改写为新的 C 函数。二者共同对接已有 void trap(struct trapframe *tf) 和 __trapret，不新增公开接口或改动其他模块。

[SPECIFICATION]
## RESTORE_ALL
Pre-Condition：当前处于 S 态 Trap 返回路径，sp 指向有效、符合 SAVE_ALL 布局的 frame；C 处理函数已经返回且保持 frame 基址可用。frame 中的返回状态有效，恢复过程不得提前开启中断；本任务只处理当前纯内核的上下文。
Post-Condition：可写通用寄存器及返回 CSR 与 frame 记录一致，sp 恢复为第 2 槽保存的原值；后续已有 sret 能从 frame 指定的位置继续执行，且不破坏 frame 之外的内存。
Case 1：C 处理函数未修改现场时，被打断执行流的通用寄存器和原栈得到恢复；x0 保持硬件规定的零值。
Case 2：C 处理函数合法修改 tf->epc 时，返回使用修改后的地址，而不是入口时保存地址的另一份副本。
Case 3：中断发生前允许或屏蔽中断时，保留 frame 中的 SPP/SPIE 等返回状态，由 sret 按处理器规则完成返回；不得无条件开启中断或强制改为用户态。
Requirements：必须与 SAVE_ALL 的槽位一一对应。恢复所需的 frame 访问完成前，不能丢失其基址；原 sp 的处理必须与其他 GPR 区分。不得把 badvaddr/cause 当作需要回写的返回 CSR，也不得改动 SAVE_ALL 来迁就恢复代码。

## __alltraps
Pre-Condition：stvec 已指向四字节对齐的 __alltraps，处理器已进入 S 态且当前内核栈有效。入口已有 SAVE_ALL 会建立完整 frame，原栈满足 C 调用的 16 字节对齐要求。
Post-Condition：trap() 恰好接收本次 frame 的地址；C 返回后 sp 仍指向同一 frame，执行流能够进入已有 __trapret，恢复处理后的现场并经 sret 返回。
Case 1：处理中断时，C 函数可读取本次保存的原因和寄存器，返回路径保留 handler 决定的现场。
Case 2：处理断点或非法指令等异常时，C 对 frame 中返回位置的合法更新能交给统一恢复流程，不另建独立的异常返回机制。
Requirements：遵守 RISC-V 参数和函数调用约定，不因调用 trap() 丢失 frame 基址或原执行流的返回地址；288 字节 frame 在原栈对齐时保持 16 字节对齐。本模块不增加中断/异常分类逻辑，填空之外的所有代码保持逐字节一致。
````

#### 实现迭代过程

先对照 `SAVE_ALL` 确定每个槽位的含义，再补全恢复宏和入口调用。检查时主要看两点：一是临时用于恢复 CSR 的 `s0/s1` 最后也要恢复成原值，二是 `sp` 必须留到最后恢复。入口则按调用约定把 frame 地址放入 `a0`，调用 `trap()` 后接回已有的 `__trapret`。

合并后用课程测试检查入口、寄存器和返回位置，相关项得分 35/35，未发现需要再次修改恢复代码的问题。

### 4.2 功能模块二：中断与异常处理（中断部分）

**负责人：** 2412819-李子谦

#### 模块功能描述

**修改文件：** `code/kern/trap/trap.c`

**需要实现/修改的函数：**

```c
void interrupt_handler(struct trapframe *tf);
```

**功能说明：**

先去掉 cause 最高位的中断标志，再按 IRQ 编号分类。这样既能处理真实硬件传入的 cause，也能处理课程测试直接传入的编号。

S 态时钟中断每次调用 `clock_set_next_event()` 设置下一次事件，再将 `ticks` 加一。每满 100 次调用已有 `print_ticks()`。其他已定义的中断输出类型提示；未知编号输出 `Unknown interrupt: cause N` 并打印现场。整个函数不修改 `trapframe`，尤其不推进 EPC，因为异步中断不应跳过原程序的指令。

手册要求达到 `SHUTDOWN_AFTER` 后关机，但没有指定数值，因此本组设为输出 10 次，即累计 1000 次时钟中断。每次打印后用 `ticks / TICK_NUM` 判断次数，达到阈值调用 `sbi_shutdown()`。宏允许编译时覆盖。

提示词把时钟、其他已定义中断和未知编号分成三种情况，前置条件是 frame 有效且初始化完成，后置条件要求 frame 不变、只有时钟分支增加 `ticks`。特别写清“10 次”指输出次数，避免与时钟中断次数混淆；所用宏和函数均来自现有头文件。

#### 最终提示词

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

#### 实现迭代过程

按提示词生成代码后，检查了 cause 掩码、计数与关机条件，以及是否影响队友的异常处理和原有分发逻辑。中断函数一次生成后通过编译和课程评分，没有再修改算法。

补充测试的接入出现过两个问题：测试替身与原函数重名，以及新增测试文件排在 `entry.S` 前面，导致裸镜像起点不再是 `kern_entry`。在临时副本中调整替身名称和文件顺序后，补充检查通过，正式入口和链接脚本保持原样。

### 4.3 功能模块二：中断与异常处理（异常部分）

**负责人：** 2412852-梁靖泽

#### 模块功能描述

**修改文件：** `code/kern/trap/trap.c` 中 `lab3 function2` 的异常部分。

**需要实现/修改的函数：**

```c
void exception_handler(struct trapframe *tf);
static uintptr_t exception_instruction_length(uintptr_t epc);
```

**功能说明：**

这部分需要在识别异常后确定正确的返回位置。`sepc` 保存的是触发异常的位置，如果直接恢复，处理器还会执行原来的故障指令。因此，我们在保存的 frame 中更新 EPC，再由模块一的恢复代码写回 `sepc`。

`exception_handler()` 对非法指令输出 `Illegal instruction at`，对断点输出 `Breakpoint at`，两者都包含修改前的 EPC，再按当前指令长度推进。其他异常调用原 `print_trapframe(tf)`，不读取 EPC 处的指令，也不修改 frame。这样，兜底分支不会为了打印一个已有的异常，再因无效取指制造新异常。

编写规格时，我们把手册的要求分成“跳过指令后继续执行”和“只做诊断”两类，分别确定输入条件和允许变化的字段：

| 对象 | 前置条件 | 后置条件与可检查的结果 |
|------|----------|------------------------|
| 指令判长 | EPC 指向可读、两字节对齐的内核代码，目标为本实验的 16/32 位指令 | 返回 2 或 4，不修改内存；32 位指令不要求四字节对齐 |
| 非法指令、断点 | frame 有效，故障指令满足判长前提 | 诊断使用原 EPC；新 EPC 等于原 EPC 加指令长度；其余字段不变 |
| 其他异常 | frame 有效，不要求 EPC 可以解引用 | 调用已有现场打印，整个 frame 不变，不取指 |

固定加 4 会越过压缩指令之后的一部分代码，固定加 2 又可能跳到普通指令中间。因此，提示词单独列出两种指令长度，并要求支持仅按两字节对齐的指令地址。同时规定只有 EPC 可以改变，便于测试时比较完整 frame。

规格也写明了适用范围：这里处理内核可读代码上的 16/32 位指令，不是完整的变长指令解码器，也不是用户地址安全取指接口。未知异常只要求诊断；保持 EPC 不变并不表示已经排除故障，真实触发时仍可能反复陷入。

#### 最终提示词

以下为异常处理的实现提示词，与 [prompt.md 对应部分](prompt.md#一异常识别与返回位置处理) 一致。

````text
[PROMPT]
任务：我负责 Lab 3 的异常部分。请补全 code/kern/trap/trap.c，让内核遇到非法指令或断点时能打印原因，跳过这条指令，再沿原来的 Trap 返回流程继续运行。
操作要求：请直接改工程里的实际文件，只动 lab3 function2 区域中的异常函数和它需要的局部辅助函数，保留 lab3 标记，在我的实现附近署 2412852。李子谦的中断处理和崔迪的 trapentry.S 都不要改，原有的分发、打印函数、头文件和课程评分工具也保持原样。
输出要求：按下面的真实接口和行为规格实现，别为了省事把所有异常都加 4。完成后说明哪些现场字段会变化，以及怎样确认返回到了下一条指令；运行过什么就写什么，不要把独立测试说成课程评分。

[RELY]
code/kern/trap/trap.h 中的定义是：
```c
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
void print_trapframe(struct trapframe *tf);
```
gpr 按 x0 到 x31 保存通用寄存器；RV64 下 uintptr_t 为 64 位无符号整数。status、epc、badvaddr、cause 分别对应保存的 sstatus、sepc、stval、scause。
code/libs/riscv.h 已有 CAUSE_ILLEGAL_INSTRUCTION=0x2 和 CAUSE_BREAKPOINT=0x3。cprintf() 已有声明，不用再写输出函数。
trap_dispatch() 已经把异常交给 exception_handler()；函数返回后，RESTORE_ALL 会把 tf->epc 恢复到 sepc，再执行 sret。
这次只考虑内核可读代码中的 16/32 位指令。压缩指令为 2 字节，普通指令为 4 字节；指令起点可能只按 2 字节对齐。不要假定 badvaddr 一定保存了指令内容。

[GUARANTEE]
这次要完成的接口：
```c
void exception_handler(struct trapframe *tf);
static uintptr_t exception_instruction_length(uintptr_t epc);
```
辅助函数只在本文件内部使用，不增加公开接口，也不修改 trapframe 的定义。

[SPECIFICATION]
## exception_instruction_length
Pre-Condition：epc 指向有效、两字节对齐的内核代码，首个半字可读，目标属于本实验使用的 16/32 位指令范围。
Post-Condition：返回该指令占用的字节数，不改代码内容或任何现场字段。
Case 1：首半字最低两位不是 11 时，返回 2。
Case 2：在上述指令范围内，首半字最低两位为 11 时，返回 4；即使 epc 不是四字节对齐也应成立。
Requirements：不要为了判长强行做要求四字节对齐的整字读取。本任务不扩展 48 位及更长指令，也不处理任意用户地址的安全取指。

## exception_handler
Pre-Condition：tf 指向有效、可写的 Trap frame，cause 已由分发函数判断为异常。对于非法指令和断点，epc 满足上面判长函数的前提；其他异常不要求 epc 可以解引用。
Post-Condition：完成对应的诊断并返回统一恢复流程。只有非法指令和断点允许改变 epc，其他字段以及 ticks 等中断状态都保持不变。
Case 1：cause 为非法指令时，输出包含 Illegal instruction at 和原 epc 的信息；返回位置等于原 epc 加本条指令的长度，不能再次执行同一条非法指令。
Case 2：cause 为断点时，输出包含 Breakpoint at 和原 epc 的信息；16 位、32 位断点分别越过 2、4 字节。
Case 3：其他 cause 调用已有 print_trapframe(tf)，不推进 epc，也不读取 epc 指向的内容；例如 epc=0 时仍只做现场打印。
Requirements：通过保存的 frame 交回返回状态，不在 C 函数里另写一套 CSR 恢复或 sret。没有要求处理的异常只负责诊断，不把它写成已经恢复成功。
````

#### 实现迭代过程

实现时把长度判断单独放进 `static` 辅助函数，避免两个异常分支各维护一套规则。实际代码读取首个 16 位半字，以最低两位判断本实验中的长度：

```c
static uintptr_t exception_instruction_length(uintptr_t epc) {
    uint16_t instruction = *(const volatile uint16_t *)epc;
    return (instruction & 0x3) == 0x3 ? 4 : 2;
}
```

两个目标分支先打印原 EPC，再调用辅助函数更新 `tf->epc`。默认分支不取指，只打印现场。生成后的实现通过了后续测试，没有因判长错误再次修改算法。

调试中遇到的主要问题在测试环境：

| 实际现象 | 分析和处理 | 修改范围 |
|----------|------------|----------|
| GCC 15.1.0 配套汇编器报 `unknown CSR sbadaddr` | 课程使用旧 CSR 名，仅在临时副本把操作数换成相同编号 `0x143` | 测试副本；正式 `trapentry.S` 不改 |
| QEMU 7.0.0 使用原 loader 方式时未进入内核 | 固件显示下一跳为零；改用 `-kernel` 后下一跳为 `0x80200000`，但仍没有内核输出，未进一步断言根因 | 测试启动命令；不改正式入口或页表 |
| 新版模拟器尝试仍未得到测试结果 | 改用已安装的课程 QEMU 4.1.1，保留相同被测异常代码，完成独立验证 | 选择课程模拟器，不为适配新版本改内核 |

补充验证分为直接调用 handler 和真正触发异常两层，分别检查 frame 的变化以及完整返回链。测试结果统一列在第五节，测试设计提示词保留在 [prompt.md](prompt.md#二异常返回与寄存器原栈的联合测试)。使用过的自编验证脚本现已删除，已有构建和运行日志保留；当前运行使用课程原有的 `make grade` 和 `make qemu`。

### 4.4 练习：stvec 后向兼容

**负责人：** 2412852-梁靖泽

手册的问题是：旧版只保存入口地址，新版用最低两位表示 MODE，为什么原来的代码还有效？

旧版入口要求四字节对齐，最低两位本来就是 `00`。新格式把低两位解释为 MODE 后，旧地址自然对应 MODE=0（Direct），BASE 和跳转位置不变，因此兼容旧代码。求 BASE 是屏蔽低两位，不是把完整地址再左移两位。

例如，假设旧入口地址为 `0x80201000`，它的低两位为 `00`。把相同的数值写入新格式的 `stvec`，MODE 仍是 0，BASE 仍为 `0x80201000`，处理器仍跳到同一入口。这个例子只用于解释编码关系，不是本次运行中 `__alltraps` 的实际地址。

Direct 模式下所有 Trap 进入 BASE；Vectored 模式下同步异常仍进入 BASE，异步中断进入 `BASE + 4 × cause_code`，其中原因编号不含最高位的中断标志。例如原因编号为 5 的异步中断使用偏移 20 字节，而不是用完整 `scause` 参与计算。

本实验 `.align(2)` 保证入口四字节对齐，`idt_init()` 将 `&__alltraps` 原样写入 `stvec`，低两位为 0，因此采用 Direct。这里没有建立向量表，不能只改 MODE 就切到 Vectored。向量表每槽四字节与故障指令本身是 16 位还是 32 位没有对应关系：前者决定中断入口偏移，后者决定异常处理应当推进多少 EPC。

### 4.5 练习：中断保护与 do-while 宏

**负责人：** 2412819-李子谦

`do { } while (0)` 让宏体执行一次，同时使整个宏在语法上相当于一条语句。调用时可以正常加分号，也能放在没有花括号的 `if/else` 中：

```c
if (need_protection)
    local_intr_save(intr_flag);
else
    other_action();
```

如果只用花括号包装，调用处的分号会多出一条空语句，破坏 `if/else` 的配对。花括号还提供了局部作用域，方便宏内部声明临时变量。

`__intr_save()` 保存的是进入前的 SIE 状态，而不是简单地关中断。恢复时分三种情况：

- 原来开启：保存真并关闭，退出时重新开启。
- 原来关闭：保存假，退出时继续保持关闭，不能无条件调用 `intr_enable()`。
- 嵌套调用：外层保存真、内层保存假，内层退出后仍关闭，直到外层退出才开启。每层需要独立的保存变量，并按相反顺序恢复。

`alloc_pages()`、`free_pages()` 和 `nr_free_pages()` 都用这种方式包住物理内存管理操作。例如，空闲链表和空闲页数可能分几步更新，若中途被同样访问这些数据的中断处理打断，就可能读到不一致的状态。本实验的时钟处理不调用分配器，但这些接口已经为临界区保留了保护。

局部关中断只防止本 hart 的可屏蔽中断打断临界区，不能阻止同步异常，也不能代替多核锁；上述恢复关系还要求临界区内部不自行开中断。

原有 `local_intr_restore(x)` 宏自带分号，没有采用同样的 `do-while` 包装，不宜直接套用上面的无花括号写法。

---

## 五、测试与验证

### 5.1 编译、运行与课程测试

在 `code/` 目录下使用以下命令编译和验证：

```bash
make clean
make
make grade
make qemu
```

11 月 4 日在李子谦的 WSL2 环境中完成整体验证，11 月 8 日在崔迪的 Docker 环境中复测，两次均使用 QEMU 4.1.1。课程测试三项全部通过，正常运行时输出 10 行 `100 ticks` 后自动关机，返回码为 0。

| 测试项 | 预期结果 | 实际结果 |
|--------|----------|----------|
| 默认内核构建 | 完成编译、链接和镜像生成 | 通过 |
| Trap 入口、寄存器恢复和断点返回 | 正确传递 frame、恢复寄存器并跳过断点 | 35/35 |
| 非法指令返回及 handler 兜底 | 按指令长度更新 EPC，其他异常打印现场 | 25/25 |
| 持续 S 态时钟中断 | 多次处理后仍能返回，计数达到 200 | 40/40 |
| 默认内核正常运行 | 内存自检通过，每 100 次中断输出一次，输出 10 次后关机 | 与预期一致 |

课程评分输出为：

```text
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
  - illegal instruction EPC advance and return: PASS (25/25)
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

课程评分运行到 `ticks=200`，尚未达到关机阈值，因此我们另用 `make qemu` 检查默认内核的关机行为。11 月 4 日的运行约 10.23 秒后正常结束。

完整记录见 [WSL 测试日志](logs/2412819-lab3-tests.txt)、[11 月 8 日编译日志](logs/20261108-recheck-build.txt)、[课程评分复测日志](logs/20261108-recheck-grade.txt) 和 [正常运行复测日志](logs/20261108-recheck-boot.txt)。

### 5.2 补充检查

除课程测试外，我们还在临时源码副本中检查了中断边界、SIE 保存恢复和异常返回，未修改正式源码及课程评分文件。

| 检查内容 | 方法 | 结果 |
|----------|------|------|
| 中断分类与计数边界 | 共 42 个用例，覆盖带或不带中断标志的原因编号、其他中断不计数，以及 99/100、199/200、999/1000 等边界；比较完整 frame | 全部通过 |
| SIE 状态恢复 | 执行实际 CSR 指令，检查原来开启、原来关闭，以及两种初始状态下的嵌套调用 | 4 个用例通过 |
| 异常函数直接调用 | 两类目标异常搭配四个指令样本，共 8 次；另外以 EPC=0 检查 13 个其他异常，只允许目标异常更新 EPC | 21 个检查通过 |
| 真实异常往返 | 分别执行 16/32 位断点和非法指令，经过完整 Trap 链，检查返回位置、32 个 GPR 槽和原栈 | 4 个用例通过 |

中断边界检查对计时、输出和关机调用使用测试替身，真实时钟及 SBI 关机由 5.1 的默认内核运行确认。异常独立测试时未使能时钟，中断处理使用替身；四条真实故障指令的起始地址模 4 均为 2，也覆盖了仅按两字节对齐的 32 位指令。

异常日志末尾为：

```text
[2412852] handler cases: 21 checks, 0 failures
[2412852] real trap 0 (32 GPR slots / original stack): PASS
[2412852] real trap 1 (32 GPR slots / original stack): PASS
[2412852] real trap 2 (32 GPR slots / original stack): PASS
[2412852] real trap 3 (32 GPR slots / original stack): PASS
[2412852] PASS (25 cases)
```

中断和 SIE 的记录见 [WSL 测试日志](logs/2412819-lab3-tests.txt)；异常验证见 [构建日志](logs/2412852-exceptions-build.txt) 和 [运行日志](logs/2412852-exceptions-qemu.txt)。

### 5.3 测试截图

以下截图由崔迪于 11 月 8 日采集。

**编译成功：**

![Lab3 编译、链接和镜像生成成功](images/2412439-lab3-build.png)

编译、链接和 `objcopy` 完成，生成 `bin/kernel` 和 `bin/ucore.img`。前者是含调试信息的 ELF，后者是供 QEMU 加载的原始二进制镜像。

**内核启动与连续时钟中断：**

![Lab3 内存自检与连续时钟输出](images/2412439-lab3-boot.png)

内存管理自检通过，时钟初始化后连续输出 `100 ticks`。启动图底部未完整显示关机过程，10 次输出和正常退出记录见 [运行复测日志](logs/20261108-recheck-boot.txt)。

**课程评分：**

![Lab3 三个课程评分项全部通过，总分 100/100](images/2412439-lab3-grade.png)

三项评分均为 PASS，总分 100/100，对应 [评分日志](logs/2412439-lab3-grade.txt)。

---

## 六、实验总结与收获

### 对操作系统的理解

1. 本实验重要的知识点及其与 OS 原理的联系、区别如下。

   | 知识点 | 实验中的实现与理解 | 与完整操作系统的区别 |
   |--------|--------------------|----------------------|
   | 中断与异常 | 两者都进入 Trap 入口，以 cause 最高位区分；中断是异步事件，异常由当前指令触发 | Trap 不一定使特权级提高，本实验主要处理 S 态到 S 态的 Trap |
   | 上下文保存与恢复 | 硬件记录返回位置、原因并更新状态位，软件把 GPR 和相关 CSR 保存为 frame；返回时恢复现场 | 这里保存的是一次 Trap 的现场，不是完整的进程调度上下文，也未保存浮点、向量状态 |
   | 异常返回位置 | 中断不推进 EPC；断点、非法指令按本实验要求跳过 2 或 4 字节 | 不能把跳过策略用于所有异常，例如缺页通常在修复映射后重试原指令 |
   | 时钟中断 | 每次重设定时事件，再计数和输出，说明处理器能持续响应事件 | 时钟可为抢占调度提供机会，但本实验还没有进程和调度器 |
   | 临界区保护 | 保存原 SIE 状态，关中断后访问内存管理数据，最后恢复原状态 | 只能屏蔽本 hart 的可屏蔽中断，不能替代多核锁，也不能阻止同步异常 |
   | 中断入口 | stvec 使用 Direct 模式指向统一汇编入口，再由 C 函数分发 | 软件中的 switch 不是硬件向量表；切换到 Vectored 模式还需要安排各入口 |

   这次实验中，保存现场和决定返回位置是两件事。前者保证寄存器不会因为处理函数而被破坏，后者决定程序是继续原来的位置，还是跳过触发异常的指令。只有两部分配合正确，打印出诊断后才能正常返回。

2. OS 原理中重要、但本实验尚未实现的内容包括：

   - 进程调度与上下文切换：当前没有就绪队列、时间片分配和进程状态切换。
   - 用户态系统调用与隔离：还没有完整的用户程序、参数检查和用户栈到内核栈切换。
   - 按需分页：已有内存管理和启动映射，但没有实现缺页后的动态映射、页面换入换出。
   - 多核同步和复杂现场管理：仍需要学习锁、内存序、嵌套 Trap 以及浮点和向量状态的保存。

### AI 协作开发的经验

提示词需要先写清接口和状态约定。例如，时钟中断次数与输出次数不是一回事，中断和异常也不能共用“EPC 加 4”的处理方式。把这些区别放进规格后，检查生成代码时就有了明确的依据。

生成代码后，我们先对照保存布局、原因编号和返回条件检查，再运行课程测试。补充测试曾因函数重名和文件链接顺序而失败，说明测试代码本身也可能改变运行条件，不能一看到失败就修改内核。课程评分通过后，我们又运行默认内核检查自动关机，补上评分未覆盖的部分。

---

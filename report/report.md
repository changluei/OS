# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4：进程管理 |
| **小组成员** | 2412439-崔迪、2412819-李子谦、2412852-梁靖泽 |
| **完成日期** |  |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412439-崔迪 | `page_insert()`、`page_remove()`、`alloc_proc()`、`proc_init()` |
| 2412819-李子谦 | `kernel_thread()`、`do_fork()`、`copy_thread()`、`copy_mm()` |
| 2412852-梁靖泽 | `cpu_idle()`、`schedule()`、`proc_run()`、`switch_to()` |

三人分别整理本人模块的规格、提示词、实现过程与测试结果，再共同完成整体联调、报告合并和 Challenge。截图由组长统一整理。完整分工见 [Lab4 分工方案](lab4-division.md)。

---

## 一、实验目的

1. 理解从页表和虚拟内存管理到内核线程创建、调度的整体关系。
2. 为内核线程申请独立的 PCB、内核栈并登记 PID，掌握失败时的资源回收。
3. 理解 trapframe 与 context 的区别，构造能沿现有恢复入口首次运行的线程现场。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2412439-崔迪 |  |  | |
| 2412819-李子谦 |  |  |  |
| 2412852-梁靖泽 |  |  | |

李子谦使用 Windows WSL2、Ubuntu 24.04.4 LTS / x86_64，RISC-V GCC 10.2.0，QEMU 4.1.1；直接在 WSL 中编译、测试，没有使用 Docker。其余成员环境由本人补充。

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

Lab4 先完善页映射和虚拟内存管理，再把内核中的执行流组织成线程，最后通过调度和上下文切换让线程真正运行。整体顺序是：

`kern_init → pmm_init → pic_init/idt_init → vmm_init → proc_init → cpu_idle → schedule → proc_run → switch_to`。

我负责的是“线程从哪里来、第一次从哪里开始执行”。创建成功只代表线程已经就绪，并不代表创建函数要直接调用线程入口；CPU 交接由调度模块完成。

### 2.2 功能的逐步实现

1. 先有物理页、页表和 `kmalloc()`，才能申请 PCB 与独立内核栈。
2. `proc_init()` 将当前执行流登记为 `idleproc`，再通过 `kernel_thread()` 创建 `initproc`。
3. `kernel_thread()` 准备入口和参数，`do_fork()` 组织 PCB、内核栈、地址空间处理、现场复制及全局登记。任何可失败的准备步骤都在登记之前完成。
4. 调度模块之后选择新线程；它的 `context.ra` 指向 `forkret`，沿 `forkret → forkrets → __trapret → kernel_thread_entry → fn(arg)` 恢复首次现场。本次专项测试检查这条返回链，但不代替完整调度联调。

---

## 四、实验内容与实现

### 功能模块一：虚拟内存页操作

**负责人：** 2412439-崔迪

本节由对应成员填写。

---

### 功能模块二：线程创建与首次运行现场

**负责人：** 2412819-李子谦

本节只包含本人四个函数；`alloc_proc()`、`proc_init()` 的实现与说明由崔迪负责。

#### 模块功能描述

**需要实现/修改的函数：**

```c
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp,
                        struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

**修改文件：** 仅 `code/kern/process/proc.c` 的 `LAB4 function2` 区域，新增四个函数，署名 `2412819`。队友的函数、公共头文件、调度与汇编文件、Makefile 和课程评分工具均未修改。

**功能说明：**

- `kernel_thread()` 将临时 trapframe 清零，用 `s0/s1` 保存函数和参数，`epc` 指向 `kernel_thread_entry`。设置 `SPP/SPIE`、清除 `SIE`，并将 `clone_flags | CLONE_VM` 交给 `do_fork()`，自身不执行线程函数。
- `copy_thread()` 在新内核栈顶部按 16 字节对齐保存独立的 trapframe，子现场的 `a0=0`；`esp=0` 时使用新现场地址作为恢复栈，否则使用传入的栈指针。清零 context，再设置 `ra=forkret`、`sp=新 tf 地址`，不保留指向父栈局部变量的引用。
- `copy_mm()` 只处理当前阶段的内核线程：`mm=NULL`，共享 `boot_pgdir_pa`。不复制用户地址空间；发现父线程 `mm` 非空时返回 `-E_INVAL`。
- `do_fork()` 先检查数量上限，再申请 PCB、记录父线程、申请内核栈、处理地址空间并复制现场。全部准备完成后，保存中断状态、分配 PID、登记链表和哈希、增加进程数、唤醒线程，最后恢复原中断状态并返回 PID。失败时按“内核栈 → PCB”的逆序清理，不登记半成品。

父线程得到的是新线程 PID，子现场中的 `a0=0` 是为恢复路径准备的返回值，两者不是同一个返回位置。当前阶段线程共享内核页表，但 PCB、内核栈和执行现场仍然各自独立。

#### 最终提示词

**一、首次现场构造：**

````text
[PROMPT]
任务：我负责 Lab4 的线程创建。请先在 code/kern/process/proc.c 中补全 kernel_thread() 和 copy_thread()，把函数和参数准备成线程第一次运行的现场。
操作要求：请用 apply_patch 直接改真实文件，只补这两个函数和必要的 static 声明，署名 2412819，保留 LAB4 标记。队友的 alloc_proc()、proc_init() 和其他模块别改，也不要新增文件、运行构建或提交 Git。
输出要求：按下面的真实接口和规格实现，最后说明改了哪里、现场怎样传递，还有哪些尚未验证。

[RELY]
请先读 proc.c、proc.h、trap.h、process/entry.S 和 trapentry.S。struct trapframe 由 gpr、status、epc、badvaddr、cause 组成，gpr 有 s0、s1、a0、sp 等字段；proc_struct 有 kstack、tf、context，context 保存 ra、sp、s0–s11。
KSTACKSIZE 是内核栈大小。已有 memset()、read_csr(sstatus)、SSTATUS_SPP/SPIE/SIE、do_fork()、forkret()。kernel_thread_entry 从 s0 取函数、s1 取参数；forkrets 从传入的 tf 恢复现场。pmm.h 定义 CLONE_VM=0x00000100，表示共享地址空间。

[GUARANTEE]
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
允许必要的文件内前置声明，不改公共结构和接口。

[SPECIFICATION]
## kernel_thread
Pre-Condition：当前线程有效，fn 是有效内核函数，arg 可为 NULL。
Post-Condition：准备清零的临时 trapframe，保留函数和参数，入口指向 kernel_thread_entry；经 do_fork(clone_flags | CLONE_VM, 0, &tf) 返回 PID 或原错误码，不留下指向局部 tf 的长期引用。
Requirements：status 保留其他位，置 SPP/SPIE、清 SIE，让新线程从 S 态入口运行；本函数只创建，不直接调用 fn。

## copy_thread
Pre-Condition：proc 已有 KSTACKSIZE 字节的独立内核栈；tf 有效；esp 为 0 或调用者提供的合法栈指针。
Post-Condition：在新内核栈顶部保存 tf 的独立副本，a0 置 0，context 从清零状态设置 ra=forkret、sp=新 tf 地址，原 tf 不变。
Case 1：esp=0 时，恢复用的 gpr.sp 指向新 tf，不能继续用父线程的栈。
Case 2：esp 非零时，gpr.sp 使用该值。
Requirements：按真实结构大小计算位置，保持 16 字节栈对齐，不增加另一套返回汇编。
````

**二、资源申请、登记与失败清理：**

````text
[PROMPT]
任务：我负责 Lab4 线程创建的另一部分。请在 code/kern/process/proc.c 中补全 copy_mm() 和 do_fork()，把 PCB、内核栈、首次现场和进程登记串起来。
操作要求：请用 apply_patch 直接修改文件，只补这两个函数及必要的 static 声明，署名 2412819，保留 LAB4 标记。已有的 kernel_thread()、copy_thread() 和队友代码不要改，不新增文件，不运行构建或提交 Git。
输出要求：以真实代码和下面的规格为准，说明资源申请、失败回收和登记顺序，不把未跑的测试写成通过。

[RELY]
请读 proc.c、proc.h、vmm.h、sync.h、error.h。proc_struct 的 parent、mm、pgdir、tf、context、list_link、hash_link 用于父子关系、地址空间、现场和登记；pgdir 是物理地址。
已有 alloc_proc()、setup_kstack()、put_kstack()、copy_thread()、get_pid()、hash_proc()、kfree()、wakeup_proc()、list_add()，以及 current、proc_list、nr_process、MAX_PROCESS、boot_pgdir_pa。local_intr_save/restore 保存和恢复原中断状态。
alloc_proc() 成功时 PCB 已清零，state=PROC_UNINIT、pid=-1、pgdir=boot_pgdir_pa；失败返回 NULL。只创建内核线程，current->mm 应为 NULL；pmm.h 中 CLONE_VM=0x00000100，本阶段不复制用户地址空间。

[GUARANTEE]
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
辅助函数只保留文件内作用域，不改公共结构。

[SPECIFICATION]
## copy_mm
Pre-Condition：current 和新 proc 有效，proc 尚未登记。
Post-Condition：内核线程共享内核页表，proc->mm 保持 NULL，pgdir 为 boot_pgdir_pa，返回 0；不分配或复制用户地址空间。
Case：current->mm 非 NULL 时返回 -E_INVAL，明确拒绝本实验未支持的情况，不把它当作成功。

## do_fork
Pre-Condition：单核环境，current 有效，tf 有效，进程管理结构已初始化；stack 为 0 或合法栈指针。
Post-Condition：成功返回唯一正 PID，parent=current；PCB、内核栈和首次现场准备完整后，才登记进程链表、PID 哈希和计数，并变为 PROC_RUNNABLE；不改变 current，不在这里调度。
Case 1：进程数达到 MAX_PROCESS，返回 -E_NO_FREE_PROC，不申请资源。
Case 2：PCB 或内核栈申请失败，返回 -E_NO_MEM，已申请的资源按逆序释放。
Case 3：copy_mm 失败，返回它的错误码，回收内核栈和 PCB。
Requirements：失败时链表、哈希和进程数不变，不释放父线程或内核共享页表；PID 分配、登记、计数及唤醒在保存中断状态的临界区完成，退出后恢复原状态。
````

两份提示词原文也保存在 [prompt.md](prompt.md)，分别发送给不继承本次聊天记录的子 agent。子 agent 根据提示词读取工程、修改对应函数，主 agent 复核并运行测试。

#### 实现迭代过程

##### 第一次迭代

**遇到的问题：**

- 最初的依赖说明漏查了 `pmm.h`，误以为没有 `CLONE_VM`。子 agent 阅读工程后指出实际定义为 `0x00000100`。
- 只实现本人的四个函数后，正式内核仍不能链接，因为 `cpu_idle()` 尚未实现。它属于队友的调度模块，不能为了让构建通过而在正式代码中代写或添加假实现。

**问题解决策略：**

- 修正提示词的真实依赖，明确共享地址空间标志；重新发送修正版，使 `kernel_thread()` 使用 `clone_flags | CLONE_VM`。上面保留的是实际发送并执行的最终版本。
- 用经过允许的临时副本做线程创建专项测试。仅在副本中添加测试入口和分配失败注入，直接检查实际四个函数；没有给工作区补上调度器或更改队友函数。

**最终结果：**

- 四个函数所在源文件交叉编译通过。
- QEMU 4.1.1 下，64、70、128、256 MiB 四种配置均通过 11 组创建专项检查。
- 真实恢复链成功在子线程内核栈上进入指定函数并正确传参。
- 正式内核的整体链接、调度联调及课程评分，仍需等待调度模块完成。未把专项测试写成整组实验已经通过。

**关键改进点总结：**

1. trapframe 必须复制到子线程自己的栈上，不能引用 `kernel_thread()` 的局部变量。
2. 在全局登记前完成可能失败的步骤，失败清理只归还本次申请的资源。
3. 不照搬其他版本的接口；`CLONE_VM`、结构布局和返回入口均以当前工程为准。

---

### 功能模块三：调度与上下文切换

**负责人：** 2412852-梁靖泽

本节由对应成员填写。

---

### Challenge：中断开关与分页模式思考题

由小组共同讨论、汇总，本人本次不代填。

---

## 五、测试与验证

以下为李子谦负责部分的实际检查，日期为 2026-11-12。

| 检查项 | 结果及覆盖范围 |
|------|----------------|
| 正式代码交叉编译 | 四个函数所在文件编译通过；全内核链接因缺少 `cpu_idle` 失败 |
| 64 / 70 / 128 / 256 MiB | 每种配置均完成 11 组专项检查，QEMU 正常退出 |
| 现场复制 | 全 frame 复制、原 frame 不变、context 清零、栈内位置与 16 字节对齐；同时检查零/非零栈参数 |
| 成功创建 | 正 PID、父子关系、独立内核栈、共享内核页表、链表和哈希登记、就绪状态、原中断状态恢复 |
| 错误处理 | 进程数上限、PCB 申请失败、栈申请失败、不支持的用户 mm；核对返回码、释放次数、空闲页与登记状态 |
| 首次恢复 | 实际执行 `forkret/forkrets/sret/kernel_thread_entry`，验证函数、参数和子栈 |
| 全量调度与课程评分 | 尚未执行，等待其他模块合入后共同验收 |

**测试方法与边界：**

测试在 `/tmp/lab4-2412819-test-*` 临时副本中进行，直接编译实际四个函数。链接包装只用于控制分配失败和统计回收；测试入口通过现有 `forkret()` 进入新线程，不实现 `schedule()`、`proc_run()` 或 `switch_to()`。因此证明了创建和首次返回现场，但不证明完整的线程切换、轮转调度或多核并发正确性。256 MiB 配置仍受框架 128 MiB 管理上限约束。

真实命令、输出和临时测试代码统一保存在 [2412819-lab4-tests.txt](logs/2412819-lab4-tests.txt)，不新增永久验证脚本。临时副本测试后删除。待队友模块完成后，再用课程原有 `make`、`make qemu`、`make grade` 共同验收；本报告不填写尚未得到的分数。

**测试截图：**

由组长在最终验收时统一补充，本次不处理。

---

## 六、实验总结与收获

### 对操作系统的理解

1. **本部分的知识点及原理联系：** PCB 管理线程身份和状态；内核栈承载独立的调用现场；context 服务于线程切换，trapframe 服务于 Trap 返回，首次运行时两者需要预先配合。共享内核地址空间不等于共享栈，`copy_mm()` 也不等于复制全部物理内存。资源生命周期和失败回滚与 OS 的资源管理原则一致，而本实验的实现范围只是单核内核线程。
2. **尚未对应上的重要知识点：** 用户进程的地址空间复制、写时复制、完整的退出与等待回收、多核同步和复杂调度策略。本次没有实现这些功能，不能把 `do_fork()` 的名字理解成已经实现完整的用户态 fork。

### AI 协作开发的经验

先把目标文件、已有接口和行为规格写清楚，再让 AI 修改真实文件。两个紧密相关的函数可以放在一份提示词中，但不同阶段分开提出，便于检查现场与资源处理。生成后重点核对栈地址、状态位、错误路径和修改边界，测试还应主动制造失败，而不是只看成功启动。依赖说明写错时要回到工程修正，不能让 AI 按错误假设继续实现。

---

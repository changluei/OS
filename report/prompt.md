# Lab 4 提示词记录

## 2412819—李子谦

负责线程创建与首次运行现场。以下两份提示词分别用于现场构造和资源管理，原文发送给子 agent；只修改本人四个函数，不代写队友模块。

### 一、构造内核线程的首次现场

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

### 二、资源申请、登记与失败清理

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



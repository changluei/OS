# Lab 4 提示词记录

## 2412439—崔迪

### 一、建立、替换和撤销单页映射

````text
[PROMPT]
完成 Lab 4 的 page_insert() 和 page_remove()。先说明两处操作的规格和计划修改位置，获得确认后只在 code/kern/mm/pmm.c 的 LAB4 function1 填空中实现，保留实验标记并署名 2412439。以现有框架和课程检查为依据。

[RELY]
pmm.h 已声明这两个函数以及 get_pte() 和 tlb_invalidate()，并提供 pte_create()、page2ppn()、pte2page()、page_ref_inc()、page_ref_dec() 及 free_page()。get_pte(pgdir, la, 1) 可按需创建中间页表，失败返回 NULL；传入 0 只查看已存在的路径。PTE_V 表示页表项有效，PTE 的权限来自调用者的 perm；error.h 定义 E_NO_MEM。框架的 check_pgdir() 和课程 PMM 检查会检查映射、权限及物理页引用计数。

[GUARANTEE]
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
使用现有签名和数据结构。写入范围仅限上述两函数的 LAB4 填空。

[SPECIFICATION]
page_insert()
前置条件：pgdir 是有效的页表根的内核虚拟地址，la 由该路径按 4 KiB 页映射，page 是可建立映射的受管理物理页。
后置条件：成功时叶级 PTE 记录 page 的物理页号和 perm，置有效位，更新受影响的 TLB 翻译并返回 0。
分支：新映射增加目标页一个引用。重复映射到同一页只更新 PTE，净引用计数不变。改映射到不同页时增加新页引用并移除旧映射，旧页失去最后一个引用时归还。若 get_pte() 返回 NULL，返回 -E_NO_MEM，不增加目标页引用或写入目标叶级映射；中间页表分配的副作用由框架 get_pte() 决定。

page_remove()
前置条件：pgdir、la 符合同样的页表访问约定。
后置条件：若有有效映射，清零叶级 PTE 并使对应 TLB 翻译失效，减少物理页一个引用，引用归零时释放物理页。
分支：页表路径不存在或叶级 PTE 无效时直接返回，不分配页表。保留中间页表供后续使用。
````

### 二、分配 PCB 与初始化 idle/init

````text
[PROMPT]
完成 Lab 4 的 alloc_proc() 和 proc_init()。先列出要修改的位置和行为，获得确认后在 code/kern/process/proc.c 的 LAB4 function2 填空补入本人两函数，保留实验标记并署名 2412439。该填空由两位成员共享，线程创建通过已有的 kernel_thread() 接口接入。

[RELY]
proc.h 定义 proc_struct、进程状态和 kernel_thread()。kmalloc()、memset()、list_init() 用来初始化 PCB。boot_pgdir_pa 是内核根页表物理地址，bootstack 是启动时使用的内核栈。proc.c 已有 proc_list、hash_list、nr_process、current、idleproc、initproc，以及 init_main()、find_proc() 和 set_proc_name()。创建子线程时，kernel_thread() 以 current 为父线程，成功返回新 PID，并由线程创建路径负责把子线程登记进链表和哈希表。

[GUARANTEE]
static struct proc_struct *alloc_proc(void);
void proc_init(void);
alloc_proc() 供同一文件的 do_fork() 复用；公共头文件和其他函数保持现有接口。

[SPECIFICATION]
alloc_proc()
前置条件：内核分配器已初始化，boot_pgdir_pa 有效。
后置条件：成功时返回尚未登记的 PCB，主体清零，state 为 PROC_UNINIT，pid 为 -1，pgdir 为 boot_pgdir_pa，两个链表节点各自初始化。其他状态由后续创建过程填写。
分支：内存申请失败返回 NULL。

proc_init()
前置条件：内存管理已初始化，内核正在启动栈上执行，进程管理子系统尚未启动。
后置条件：初始化全局链表和 PID 哈希桶。idleproc 使用 PID 0、bootstack 和 PROC_RUNNABLE，设置 need_resched，并成为 current。把 idle 计入进程数，保留普通进程链表给已登记的普通线程。用 kernel_thread() 创建以 init_main 为入口的线程，按返回 PID 找到 initproc 并设定名称。CPU 交接由后续调度完成。
分支：idle PCB 分配或内核线程创建失败时报告启动错误；按 PID 查询 init 失败时触发断言。队友入口尚未完成时先检查两个源文件的编译，完整启动与评分在合入后进行。
````

## 2412852—梁靖泽

### 一、单核线程调度链路

````text
[PROMPT]
任务：完成 Lab 4 单核内核线程的协作调度链路，让 idle 根据调度请求让出 CPU，按循环顺序选择 runnable 线程，并安全切换当前线程和 Sv39 页表。只修改 code/kern/process/proc.c 的 LAB4 function3 区域（cpu_idle、proc_run）和 code/kern/schedule/sched.c 的 LAB4 function3 区域（schedule）。
操作要求：在你的工作区用 apply_patch 修改真实文件；保留其他成员代码和公开接口，只将本人区域标记署名为 LAB4 function3 2412852，不新增代码内注释，不修改 switch.S（另一子 agent 负责），不编写文档，不提交 Git。可以运行针对性验证，但不能为通过验证修改其他模块。
输出要求：以工程实际定义和下列行为规格为准，直接完成代码，最后列出修改路径、关键实现和真实验证结果/未验证项。不要把步骤式伪代码当成规格。

[RELY]
读取 proc.c、proc.h、sched.c、sched.h、sync.h、riscv.h、pmm.h，以及 pmm.c 的 enable_paging()。current、idleproc 和 proc_list 是现有全局对象；idleproc 不在 proc_list 中。proc_struct 的 state（PROC_UNINIT/PROC_SLEEPING/PROC_RUNNABLE/PROC_ZOMBIE）、runs、volatile bool need_resched、list_link、context、uintptr_t pgdir 为相关字段；pgdir 是根页表物理地址，不是内核虚拟地址。list_next、list_empty、le2proc 为现有链表接口。local_intr_save/restore 保存和恢复原 SIE 状态，支持嵌套临界区。switch_to(struct context *from, struct context *to) 已在 proc.c 声明，其汇编由另一个 agent 完成。RISCV_PGSHIFT=12；pmm.c enable_paging 使用 write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT))，需核对现有 flush_tlb() 接口。libs/riscv.h 的 lsatp(unsigned int) 是 Sv32 遗留，严禁调用。当前所有内核线程共享 boot_pgdir_pa，但仍须实现通用页表切换语义。无需实现抢占时钟策略或进程退出。

[GUARANTEE]
void cpu_idle(void);（保留 proc.h 的 noreturn 声明）
void schedule(void);
void proc_run(struct proc_struct *proc);
不修改公开签名或结构布局，无需新增辅助函数；若确有需要，只允许所属文件内 static 辅助函数。

[SPECIFICATION]
## cpu_idle
Pre-Condition：proc_init 已完成，current 为 idleproc，进程列表有效，单核内核环境。
Post-Condition：永久执行 idle 循环；当 current->need_resched 非零时请求 schedule，无调度请求时不调用 schedule；不得返回或自行改动进程生命周期。
Requirements：不无条件关闭中断，不增加未约定的 WFI/抢占机制。
## schedule
Pre-Condition：current、idleproc、proc_list 已初始化；普通进程的 list_link 属于 proc_list，idleproc 不在列表中。
Post-Condition：在受中断保护的临界区处理一次调度，清除发起调度线程的 need_resched；从当前普通线程之后开始循环扫描，idle 发起时从表头后开始；选择第一个 PROC_RUNNABLE 线程，绕过睡眠、未初始化和僵尸线程；目标 runs 增加一次，只有目标不同于 current 时调用 proc_run。
Case 1：列表为空或没有 runnable 普通线程，目标为 idleproc。
Case 2：扫描绕回后当前普通线程是唯一 runnable，允许继续选择自身，更新 runs 但不切换上下文。
Case 3：有多个 runnable 普通线程，依链表循环顺序轮转，不优先重复选当前线程，不把链表哨兵解释为 PCB。
Requirements：选择、need_resched、runs 维护和切换请求整体处于临界区；本线程从切换恢复后恢复调用 schedule 前的中断开关状态；不重排进程列表或改变线程 state。
## proc_run
Pre-Condition：proc 非 NULL 且 PROC_RUNNABLE，current 有效；proc->pgdir 是合法、页对齐的 Sv39 根页表物理地址；新旧 context 均符合 switch_to 约定。
Post-Condition：目标不同时，以原 current 为保存现场的对象，current 指向目标，satp 采用 RV64 Sv39 模式和目标根页表 PPN，TLB 失效后进入目标 context；原线程以后恢复时可正常从本调用返回，并恢复进入时的中断状态。
Case 1：proc==current 时无上下文或页表切换，无全局状态变化。
Case 2：目标不同，current、satp、TLB、寄存器上下文切换保持一致，切换窗口不可被中断观察到中间状态。
Requirements：satp 使用 64 位模式位，不能截断物理地址或沿用 Sv32 lsatp；TLB 刷新发生在页表切换后和目标现场执行前；不改 runs（由 schedule 负责），不保存 trapframe 或新增栈切换入口。
````

### 二、RV64 上下文保存与恢复

````text
[PROMPT]
任务：实现 Lab 4 RV64 内核线程上下文切换底层能力，与 C 层 proc_run 和线程首次运行现场兼容。唯一可写文件为 code/kern/process/switch.S，补全 LAB4 function3 区域。
操作要求：用 apply_patch 修改实际文件，标记署名 LAB4 function3 2412852；保留公开接口和其他代码，不新增代码内注释，不修改 proc.c、sched.c、proc.h、Trap 或入口汇编（调度链路由另一子 agent 完成），不编写文档，不提交 Git。允许针对性汇编/反汇编验证，不改其他模块以绕过问题。
输出要求：使用下面真实结构和行为规格完成代码，最后列出改动文件、寄存器布局依据以及真实验证/未验证项。

[RELY]
读取 code/kern/process/proc.h、proc.c、entry.S、switch.S 和 kern/trap/trapentry.S。struct context 按以下顺序含十四个 uintptr_t 字段：ra、sp、s0、s1、s2、s3、s4、s5、s6、s7、s8、s9、s10、s11；RV64 每字段 8 字节。proc.c 已声明 void switch_to(struct context *from, struct context *to)。RV64 ABI 用 a0 传 from，a1 传 to；C 调用者负责中断临界区、current 和页表切换。copy_thread 为新线程构造 context.ra=forkret、context.sp=新线程内核栈上的 trapframe 地址，forkret 经 forkrets 恢复 trapframe。常规切换需返回旧线程 switch_to 调用后的 C 续执行点。

[GUARANTEE]
void switch_to(struct context *from, struct context *to);（用 .globl switch_to 导出同名汇编入口）
不新增公开接口，不改变 struct context，不新增辅助函数或另一个 Trap 恢复入口。

[SPECIFICATION]
## switch_to
Pre-Condition：RV64 S 态，from、to 指向不同的有效 context，均有足够存储并符合 uintptr_t 对齐；调用者已保护临界区并完成 current 与页表切换；目标 sp、ra 为合法恢复现场。
Post-Condition：from 精确保留本次调用的 ra、sp、s0–s11；上述十四个寄存器从 to 恢复；控制流进入目标 ra，使用目标栈。目标 context 本身只读，其他进程/内存不受影响。
Case 1：目标为已有线程，恢复它上次保存的现场，使其像从此前 switch_to 返回一样继续运行。
Case 2：目标为新线程，使用 copy_thread 构造的 sp 和 ra 进入 forkret，再沿现有 forkrets/kernel_thread_entry 链路首次运行，不能额外调整 trapframe 或自行调用线程函数。
Requirements：严格按 proc.h 布局使用 RV64 64 位访存；不能漏保存/恢复 s0 或 s11，不能截断 ra/sp；无需保存 ABI caller-saved 寄存器、gp/tp、浮点寄存器或 CSR；访存期间 a0/a1 对应的 context 指针仍有效。不得操作 satp、中断开关或 current，不建立自己的额外栈帧。
````

## 2412819—李子谦

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


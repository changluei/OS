# Lab 5 编码提示词 · 2412852 梁靖泽

以下是按课程 Lab 0.5 编写并发给子 agent 的原始提示词。提示词中的多进程用例是验收要求，个人报告记录当前分支可独立复现的结果。 [提示词规则](../../lab2026/_book/lab0.5/2_prompt_engineering.html) · [四段式模板](../../lab2026/_book/lab0.5/3_prompt_structure.html)

## 模块一 · 用户态系统调用

```text
[PROMPT]
任务：把 Lab 5 的 RV64 用户态 ecall 接到内核分发、进程服务和 Trap 返回。完成 code/kern/trap/trap.c 的模块一异常入口、code/kern/syscall/syscall.c 的分发与九个处理函数。必要时修正 code/user/libs/syscall.c 中同一 ABI 的发起端，或在 code/kern/mm/pmm.c 末尾补齐已有的 print_pgdir 声明所对应的只读诊断实现。由同一独立子 agent 按下方模块四提示词实现进程控制及所需的用户态时钟调度。
操作要求：直接编辑真实工作区。保留 lab5 注释，用 2412852 替换所属 YOUR CODE。保留模块二的 ELF、wait 和模块三的 copy_mm、do_fork、dup_mmap、copy_range；不写 COW 或其测试，不改课程测试或 Makefile，不写报告，不提交 Git。代码辅助函数保持文件内部作用域。可在 /tmp 建独立副本，以免测试构建或临时代码污染工作区。
输出要求：逐项给出实际改动、交叉编译和 QEMU 验证结果、遇到的 bug 及未覆盖部分。请记录编译器和 QEMU 版本。遇到其他模块依赖时明确区分真实代码和临时联调层。

[RELY]
libs/unistd.h 中课程已注册 exit、fork、wait、exec、yield、kill、getpid、putc、pgdir，syscalls 用指定下标建稀疏表。a0 为调用号，a1–a5 为五个 XLEN 参数槽。trap() 在派发前将当前帧挂到 current->tf，结束时恢复旧帧，用户态返回前处理 PF_EXITING 与 need_resched。trapentry.S 已根据 SPP 恢复上下文和 sscratch，不需要改动。CAUSE_USER_ECALL 是 8，ecall 长 4 字节。
proc.h 已声明 do_exit、do_fork、do_wait、do_execve、do_yield、do_kill。copy_thread 令子进程保存的 a0 为 0。do_wait 和 do_execve 在实际使用指针前借助 mapped_mem_check 校验 VMA 与有效用户 PTE，do_wait 另用 copy_to_user。load_icode 成功时就地将当前帧替换为新程序的入口、栈和状态。cputchar 可输出控制台字符。pmm.h 声明 print_pgdir(void)，当前树还没有对应定义，核对实际代码。tools/test_lab5.c 检查 getpid、非法编号和 epc；tools/grade.sh 有用户程序现有输出约束。

[GUARANTEE]
void exception_handler(struct trapframe *tf);
void syscall(void);
static int sys_exit(uint64_t arg[]);
static int sys_fork(uint64_t arg[]);
static int sys_wait(uint64_t arg[]);
static int sys_exec(uint64_t arg[]);
static int sys_yield(uint64_t arg[]);
static int sys_kill(uint64_t arg[]);
static int sys_getpid(uint64_t arg[]);
static int sys_putc(uint64_t arg[]);
static int sys_pgdir(uint64_t arg[]);
如需补全已有 print_pgdir 或修正用户库，保留其已有公开接口和调用号。

[SPECIFICATION]
exception_handler
Pre-Condition：tf 为同步异常现场；U 态系统调用具有有效的 current 和 current->tf。
Post-Condition：U 态 ecall 令保存的 epc 前移 4，再进入 syscall。返回时使用交付给 Trap 恢复的现场。
Case：Lab 3 的内核态非法指令、断点及其他诊断行为保持原语义，S 态 ecall 不进入用户分发表。无法处理的用户异常终止故障进程，以免在同一故障指令上循环。普通缺页的兜底退出路径应便于模块三在此前接入可恢复的 COW 缺页处理，不实现 COW。用户故障后 init 仍应回收进程并通过空闲页检查。
syscall
Pre-Condition：当前 PCB 挂有 Trapframe，寄存器来自用户态。
Post-Condition：使用完整 XLEN 宽度验证下标，已注册调用派发一次；正常返回将 int 结果携带至保存的 a0。未知、空洞、负值位形和超出表长的编号返回 -E_INVAL。
Requirements：验证必须在任何下标访问前完成。成功 exec 的新入口、sp、status 不被旧帧覆盖。返回负数应保留有符号语义。
sys_exit、sys_fork、sys_wait、sys_exec、sys_yield、sys_kill
Pre-Condition：arg 至少有 a1–a5 的快照。
Post-Condition：依次转交 error_code，fork 的默认 flags/当前用户 sp/当前帧，wait 的 pid/可选退出码指针，exec 的 name/len/binary/size，yield，以及 kill 的 pid；传递服务返回值。指针和长度按 RV64 宽度传递，复用 do_wait/do_execve 的校验，封装不解引用用户指针。传入超出 int 表示范围的 wait/kill PID 应拒绝，避免截断到另一个有效 PID。
sys_getpid、sys_putc、sys_pgdir
Pre-Condition：current 有效。
Post-Condition：分别返回当前 PID、写出一个字符后返回 0、只读展示当前用户页表后返回 0。页表展示限定于已映射的用户页与权限；不分配页表，也不暴露内核地址或物理地址。
用户侧调用桩
Pre-Condition：维持现有用户库入口类型和 a0/a1–a5 约定。
Post-Condition：仅依赖实参提供的值，未用参数明确初始化；汇编对 C 编译器声明寄存器数据流，返回 32 位 int 时不以 64 位访存覆盖相邻栈数据。用实际反汇编或优化级别下的执行验证，避免裸 va_arg 读取未传入参数和违规宽度写回。
```

## 模块四 · 进程退出、让出与终止

```text
[PROMPT]
任务：在 code/kern/process/proc.c 的模块四标记处让用户进程能够安全退出、主动让出 CPU、请求终止另一进程，与现有 do_wait 构成回收闭环。允许在 code/kern/trap/trap.c 中补充该功能所需的用户态时钟调度点，使纯用户忙循环可在单核上交回 CPU。
操作要求：直接编辑工作区并保留 lab5 注释，署名 2412852。沿用现有数据结构与调度方式，不改 do_wait、fork 复制、公开接口、Trap 汇编或课程测试。报告由主 agent 负责。允许用 /tmp 副本运行联调。本分支 copy_mm 目前拒绝用户 fork，独立副本可以使用只供测试的深复制接线；该接线不得进入交付文件。
输出要求：验证 yield 及 Process Control 自检；在可用的 fork 联调环境中验证 exit、badarg、spin、waitkill。至少从状态或日志验证退出码写回、挂起后唤醒、孤儿接管和页回收。说明修改前可重现的问题、修正和实际结果。

[RELY]
proc_struct 的 mm、pgdir、state、exit_code、flags、need_resched、wait_state、parent、cptr、optr、yptr 描述资源与进程树。cptr 指向最年轻的孩子，optr 指向更年长的兄弟，yptr 反向；set_links 和 remove_links 同时维护全局登记及 nr_process，单纯过继不得再次登记 PID 或改变计数。find_proc 仅查正 PID。WT_CHILD 带 WT_INTERRUPTED。wakeup_proc 唤醒非僵尸并清 wait_state，调用前先确认目标确实在相应状态。
vmm.h 提供 mm_count_dec、exit_mmap、mm_destroy，proc.c 有 put_pgdir，libs/riscv.h 的 lsatp 接收 Sv39 根页表物理地址，另有 flush_tlb。exit_mmap 和 mm_destroy 要求 mm 引用归零。do_wait 在关中断区检查关系与僵尸状态，复制退出码后移除 PID、PCB 与内核栈；被唤醒后若有 PF_EXITING 会 do_exit(-E_KILLED)。schedule 只选择 PROC_RUNNABLE，清除发起者的 need_resched。trap() 只在返回 U 态前处理 PF_EXITING 和 need_resched。当前 IRQ_S_TIMER 仅维护事件与计数，spin 和 waitkill 的忙循环没有 ecall，不设置用户态时钟调度请求就会卡住。

[GUARANTEE]
int do_exit(int error_code);
int do_yield(void);
int do_kill(int pid);
必要时更新 void interrupt_handler(struct trapframe *tf) 的 IRQ_S_TIMER 分支；保留已有时钟重装和日志的语义。

[SPECIFICATION]
do_exit
Pre-Condition：当前进程已登记，有合法父进程。idle 和 init 受保护，调用它们的退出路径属于内核错误。
Post-Condition：合法退出不再回到调用者。当前 PCB 脱离用户 mm，活动 satp 切回启动根页表并刷新 TLB，mm 共享引用减一次，仅最后一个所有者离开时回收用户页、页表和 mm。僵尸保留退出码、PID、PCB 与当前内核栈供父进程回收。
Case 1：父进程在 WT_CHILD 等待时可继续运行并看见完整的退出状态。
Case 2：仍有后代时，把全部直接孩子过继给 init，保持双向兄弟链接正确；已有僵尸孩子可唤醒正在等待的 init。过继前后子进程 PID、全局登记与计数一致。
Requirements：页表切换先于任何相关页表释放。退出码、僵尸发布、父进程唤醒和过继在受单核中断保护的临界区内一致地完成。已成为僵尸的当前进程立即交给调度器。
do_yield
Pre-Condition：current 有效。
Post-Condition：设置当前 need_resched 并返回 0，由现有用户态 Trap 返回路径调度。
do_kill
Pre-Condition：按 int PID 请求。
Post-Condition：有效目标首次收到请求后置 PF_EXITING 并返回 0；可中断睡眠的目标被唤醒以便自行执行退出；目标的退出码由终结路径设置为 -E_KILLED。
Case：非法、未登记、idle/init 和已成僵尸的目标返回错误且不修改状态。重复请求返回 -E_KILLED。tools/test_lab5.c 明确要求 do_kill(-1) == -E_INVAL。
Requirements：查询、状态检查、置位和唤醒在同一个临界区完成。请求者不释放目标仍可能持有的地址空间或内核栈。
时钟调度点
Pre-Condition：IRQ_S_TIMER 正常重装，tf 记录中断前特权级。
Post-Condition：来自 U 态的时钟中断登记重调度请求，沿既有 Trap 尾部进行切换或处理 PF_EXITING；S 态定时中断不引入异步内核抢占。保留计数、时钟和评分钩子的语义。
```

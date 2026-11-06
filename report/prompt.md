# Lab 3 提示词记录

本文按成员保存提示词。梁靖泽部分分为异常实现、异常测试、模块一说明整理、`stvec` 思考题和报告整合；编程任务与文档任务分别处理，不把资料整理算成重新实现。以下是依据实验手册和当前代码整理的最终版本，不是逐字还原的历史对话。

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

## 2412852—梁靖泽

### 一、异常识别与返回位置处理

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

### 二、异常返回与寄存器、原栈的联合测试

````text
[PROMPT]
任务：异常函数写完以后，我想确认的不只是“打印出来了”，还要确认它确实跳过了正确的字节数，返回后寄存器和栈没有被弄坏。请在 code/tools/verify_exceptions_2412852.py 中实现或完善一套独立测试。
操作要求：直接修改这个测试文件，所有测试接入都放在 /tmp 下的临时源码副本中，不改正式内核、Makefile、grade.sh、inject.py 和 test_lab3*。中断代码如果还没合并，可以只在副本里放替身；如果已经有 interrupt_handler()，不要再定义一个同名函数。这次测试不使能时钟，也不改队友的中断逻辑。
输出要求：保存真实的构建输出和 QEMU 串口输出，按学号区分文件。告诉我每组检查在验证什么，失败就保留失败原因，不要只有一句“测试通过”。

[RELY]
实际被测代码是 trap.c 中的 exception_handler()，真实返回链是 trapentry.S 中的 __alltraps、RESTORE_ALL 和 sret。
RV64 的 trapframe 有 36 个 8 字节槽；前 32 槽对应 x0～x31，后四槽依次是 status、epc、badvaddr、cause。保存的 sp 是进入 Trap 前的值，不是 frame 基址。
现有测试工具使用 Python 标准库、RISC-V GCC 和 QEMU。课程环境使用 QEMU 4.1.1，命令行可通过 --qemu 指定路径，--log-dir 指定日志目录。
可用编码是 16 位断点 0x9002、32 位断点 0x00100073、16 位非法编码 0x0000，以及本目标平台未实现的 32 位编码 0x0000007b。
有些新汇编器不接受课程旧名 sbadaddr；如确实遇到这个报错，只在测试副本把对应 CSR 操作数写成同一编号 0x143，并在日志中说明，不动正式汇编。

[GUARANTEE]
保留宿主测试入口：
```python
def context_assembly():
def main():
```
context_assembly() 返回供临时副本编译的汇编源码；main() 负责隔离接入、构建、运行和记录结果。可增加模块内部辅助函数，不给正式内核增加接口。
临时测试镜像需提供：
```c
void verify_exceptions_2412852(void);
uintptr_t verify_break16(void);
uintptr_t verify_break32(void);
uintptr_t verify_illegal16(void);
uintptr_t verify_illegal32(void);
```

[SPECIFICATION]
## context_assembly / 四个 verify_* 汇编测试函数
Pre-Condition：测试在 RV64 S 态内核中执行，入口和异常函数已就绪，测试栈可用并满足调用约定。
Post-Condition：每个函数真正触发指定异常，返回后比较全部 32 个 GPR 槽和原 sp；检查通过返回 0，不一致返回非零。测试函数本身不能破坏 C 调用者的被调用者保存寄存器。
Case 1：16/32 位断点各触发一次，必须到达断点后面的检查代码。
Case 2：16/32 位非法指令各触发一次，也必须回到下一条指令，不能陷入重复异常。
Requirements：四条测试指令都覆盖起点模 4 等于 2 的情况，尤其不能漏掉非四字节对齐的 32 位指令。比较中包含 ra、sp、gp、tp，不仅检查几个临时寄存器；不要为测试随意破坏 gp、tp。

## verify_exceptions_2412852
Pre-Condition：frame 与指令样本在有效内存中，真实 Trap 测试安排在 clock_init()/intr_enable() 之前。
Post-Condition：每组给出检查数和失败数，最终只有全部断言满足时才输出带本人学号的 PASS。
Case 1：两类目标 cause 分别搭配四个 16/32 位样本，共 8 次直接调用；对比整个 frame，除预期的 epc 外不能出现变化。
Case 2：对 cause=0、1、4、5、6、7、8、9、11、12、13、15、20 共 13 个其他异常，以 epc=0 调用；必须完成诊断且 frame 不变，不能因为统一取指产生新的异常。
Case 3：执行上面的四个真实 Trap 测试。21 个直接调用加 4 个真实往返，共 25 个用例，分别报告结果。

## main
Pre-Condition：能够找到工程和所选工具链，日志目录可写。
Post-Condition：正式源码和默认构建流程不变，临时副本用后清理，构建和运行日志可供核对。
Case 1：成功时保存工具版本和原始输出，日志中应有 21 个直接调用零失败、四次真实返回 PASS 和总计 25 个用例的 PASS。
Case 2：编译失败、程序崩溃、出现 FAIL，或超时前没有完整成功标志，都按失败退出。测试内核成功后主动空转的情况下，可以在已经收到完整成功标志后由宿主超时结束，但超时本身不算通过。
Requirements：不要覆盖之前的成功日志来掩盖新失败。复测可用 --log-dir 另存一份；这组结果只证明异常与现场恢复，不代表时钟、关机或课程总评分。
````

### 三、整理模块一的实现说明与已有验证材料

这部分是文档整理，不是让 AI 重新生成崔迪的汇编。

````text
[PROMPT]
任务：我负责把模块一的说明并进报告，但 RESTORE_ALL 和 __alltraps 是崔迪写的。请结合真实源码，帮我把它们和我的异常函数之间的关系讲清楚。
操作要求：直接更新 report/report.md 的模块一说明，只读 code/kern/trap/trapentry.S、trap.h、trap.c 和 README 中的已有记录，不改汇编或重新署名。别把手册里的用户态示例照搬成本实验已经实现的功能。
输出要求：按报告里的“模块功能描述、规格与提示词整理、验证材料”来写。需要解释到能对照源码看懂，不用把每条 LOAD 指令都翻译一遍。

[RELY]
SAVE_ALL 已给定布局，RV64 共 36 个槽、288 字节；第 2 槽是原 sp，第 32/33 槽是 sstatus/sepc，第 34/35 槽是诊断字段。
__alltraps 调用 void trap(struct trapframe *tf)，返回经 __trapret、RESTORE_ALL、sret。现有实现者署名 2412439。
README 记录了两版 QEMU 各 16/16 组独立测试，但原临时脚本和完整原始日志不在当前仓库。我的异常日志另有四次真实 Trap 的联合检查，李子谦的整体验收日志另有课程评分。

[GUARANTEE]
不新增、修改代码接口；说明对象是 RESTORE_ALL、__alltraps、trap(struct trapframe *tf) 和 __trapret。
交付模块一正文、调用关系和验证来源说明，保留“崔迪实现，梁靖泽整理”的归属。

[SPECIFICATION]
## 模块一说明
Pre-Condition：以现有 SAVE_ALL、frame 定义和实际恢复代码为依据，缺失的原始对话不自行补写。
Post-Condition：能说明 frame 怎样传给 C、为什么最后恢复 sp、C 返回后怎样恢复处理器状态，以及这与异常修改 epc 的关系。
Case 1：解释正常的 S 态 Trap 往返；指出 x0 不用恢复，诊断 CSR 不回写，288 字节分配保持原栈的 16 字节对齐。
Case 2：对于手册的 U 态返回示例，说明 sret 按 SPP 决定特权级，本实验没有额外实现用户页映射或双栈切换。
Case 3：已有 16/16 测试只注明 README 的记录来源；我自己的联合检查和李子谦的课程评分分别引用对应日志，不混成同一次测试。
Requirements：不要把现在整理的提示词写成崔迪当时实际使用过的提示词，也不要把报告整理算成我的汇编实现。
````

### 四、stvec 后向兼容思考题

````text
[PROMPT]
任务：我来回答手册里 stvec 低两位改作模式位后为什么还能兼容旧代码这道题。请结合工程里的 idt_init() 和 __alltraps，把答案写进 report/report.md 对应练习。
操作要求：直接改文档，不改初始化代码或入口对齐。用正常的实验报告语气解释，别只写一句“因为向后兼容”。
输出要求：先说清旧地址和新编码的关系，再解释 Direct、Vectored 和我们实际用的是哪一种，给一个能算清楚的地址例子。

[RELY]
手册给出的旧版约束是 stvec 按四字节对齐；新格式最低两位为 MODE，其余位表示对齐的 BASE。MODE=0 为 Direct，MODE=1 为 Vectored。
工程的 idt_init() 将 &__alltraps 原样写入 stvec，入口有 .align(2)。RV64 下地址宽度为 64 位，cause 最高位是中断标志。

[GUARANTEE]
不新增接口；分析现有 void idt_init(void) 和 __alltraps 的对应关系，交付“练习：stvec 后向兼容”的完整答案。

[SPECIFICATION]
## stvec 思考题答案
Pre-Condition：区分完整地址、BASE 字段和 MODE，不把已完整对齐的地址再左移两位。
Post-Condition：能由旧地址最低两位为 00 推出新格式 MODE=0、BASE 不变，说明旧的直接入口语义得到保留。
Case 1：用一个四字节对齐的示例地址说明原值不变时仍进入同一入口；示例地址不要冒充日志里的实际符号地址。
Case 2：说明 Direct 下所有 Trap 到 BASE；Vectored 下同步异常仍到 BASE，异步中断到 BASE+4×原因编号，编号不带最高位中断标志。
Case 3：回到本工程，说明 .align(2) 和原样写地址为什么对应 Direct；没有向量表时，不能只把 MODE 改成 1 就认为入口仍能正常使用。
Requirements：把向量表的四字节槽间距和故障指令的 16/32 位长度区分开，本题不引出用户态 Challenge。
````

### 五、原理总结与本人报告内容整合

````text
[PROMPT]
任务：请把我负责的异常、测试、stvec 答案和模块一整理内容放回报告模板。我希望正文能讲清自己理解了什么，而不是一份操作清单。
操作要求：直接修改 report/report.md，保留“基本信息、实验目的、实验环境、整体逻辑、内容与实现、测试与验证、总结与收获”的现有结构。李子谦写好的中断、关机和中断保护答案不要重写，只补我负责的部分以及必要的衔接。
输出要求：每个编程部分要有功能说明、spec 怎么确定、最终提示词、实际实现和调试、验证结果。原理总结用自己的话解释联系和差别，别简单堆术语。

[RELY]
本人分工以 README 为准，代码以 trap.c、trapentry.S 和 trap.h 为准。prompt.md 前四部分是这次整理的提示词，不是完整历史聊天记录。
异常构建和串口证据在 report/logs/2412852-exceptions-build.txt、2412852-exceptions-qemu.txt；整体验收来源是李子谦的 2412819-lab3-tests.txt。旧 Lab 2 的日志和图片不能混作 Lab 3 证据。
Lab 3 要求解释整体逻辑、核心模块、spec/prompt、真实 bug 与修复、实验知识点同 OS 原理的联系及差异，以及未覆盖的重要知识点。

[GUARANTEE]
不修改代码接口。交付本人负责的报告正文，包含与 prompt.md 对应的提示词、测试证据链接、stvec 答案和原理总结，保留其他成员已有内容。

[SPECIFICATION]
## 报告整合
Pre-Condition：先核对源码、日志和分工。日志只能证明其中实际记录的版本与测试，不把设计中的用例直接写成已通过。
Post-Condition：读者能分清谁实现了什么、哪些结果是我自己的独立检查、哪些来自队友整体验收；报告章节与模板对应。
Case 1：异常部分说明为什么不能固定 epc+4，为什么只允许 epc 变化，以及“直接调用通过”和“真实 Trap 能返回”各能证明什么。
Case 2：调试只写真实遇到的旧 CSR 别名和模拟器启动问题；没有发生的算法错误不编造。脚本在合并后需要适配的地方另写成复现限制，不冒充当时已修好。
Case 3：总结 Trap 与 interrupt/exception、硬件 CSR 与软件现场、SPP 与返回特权级、异常跳过与缺页重试的关系；列出调度、用户态系统调用、按需分页和多核同步等未覆盖内容，并说明缺在哪里。
Requirements：现有证据缺截图或模型名称时如实标注，不拿旧图或猜测补齐。写成正常学生的实验分析，不把全文写成给 AI 的命令或文件修改记录。
````

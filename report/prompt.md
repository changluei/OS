# Lab 2 规范化提示词

## 一、物理内存探测与页描述符初始化

负责人：2412439-崔迪。

````text
[PROMPT]
**任务**：补全 code/kern/mm/pmm.c 中的物理内存初始化功能，根据设备树提供的 RAM 信息建立页描述符布局，排除已有占用，将完整空闲页面交给当前物理内存管理器。
**操作要求**：必须修改工程中的实际文件，只补充 page_init() 的实验标记位置；init_pmm_manager() 仅按实验要求选择 best_fit_pmm_manager。保留 lab2 标记，将本人填空位置的 YOUR CODE 替换为 2412439。不得改动入口汇编、DTB 解析、公共头文件、pmm_init()、Makefile 或课程测试。
**输出要求**：依据以下真实接口和行为规格完成实现，说明实际修改范围、边界处理和验证结果；未执行的检查应明确注明。

[RELY]
code/kern/driver/dtb.h 已提供：
```c
uint64_t get_memory_base(void);
uint64_t get_memory_size(void);
```
code/kern/mm/memlayout.h 中的真实描述符布局为：
```c
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};
```
现有宏定义：
```c
#define PGSIZE 4096
#define PGSHIFT 12
#define DRAM_BASE 0x80000000
#define KERNEL_BEGIN_PADDR 0x80200000
#define PHYSICAL_MEMORY_END 0x88000000
#define PHYSICAL_MEMORY_OFFSET 0xFFFFFFFF40000000
```
pmm.c 已有 pages、npage、nbase、va_pa_offset；其中 nbase = DRAM_BASE / PGSIZE，pages[i] 对应物理页号 nbase + i。链接器符号 end[] 是内核末端的虚拟地址。PADDR() 使用 va_pa_offset 进行虚实转换，PPN() 提取页号，ROUNDUP()/ROUNDDOWN() 用于对齐。可调用现有 init_memmap(struct Page *base, size_t n)、pa2page()、页状态宏、set_page_ref()、list_init() 和 panic()；这些依赖均保持原样。

[GUARANTEE]
必须补全或按限定范围修改：
```c
static void page_init(void);
static void init_pmm_manager(void);
```
允许在学生实现区域增加必要的局部变量或 static 辅助函数，不新增公共接口。

[SPECIFICATION]
## page_init
**Pre-Condition**：启动映射已建立，DTB 解析完成，当前 pmm_manager 已初始化但尚未接管空闲页；调用处于单核启动阶段。
**Post-Condition**：va_pa_offset 正确描述固定映射；npage 是可管理物理页号的排他上界；pages 指向有效描述符数组，数组包含 npage - nbase 个元素。固件、内核、描述符及其他未交付区间保持保留状态，分配器仅接管排除已有占用后的完整空闲页。
**Case 1**：RAM 范围合法且能够容纳内核和全部描述符时，描述符布局有效，引用计数等初始状态正确，空闲区间不与已用空间重叠。
**Case 2**：RAM 超出 PHYSICAL_MEMORY_END，或末端不是页边界时，仅管理框架范围内的完整页面，不登记被截断部分或不完整尾页。
**Case 3**：描述符可以完整放置，但其后不足一个完整空闲页时，不调用 init_memmap()，不将零长度区间交给分配器。
**Case 4**：RAM 基址或长度为零、范围加法溢出、未覆盖内核，或描述符无法放入管理范围时，通过 panic() 终止；在此之前不得越界写描述符或注册无效空闲区。
**Requirements**：区分虚拟地址、物理地址、物理页号和描述符指针；不能把 npage 直接作为数组元素数。对齐和地址转换不得引入溢出，使用 PADDR() 前必须满足其偏移前提。

## init_pmm_manager
**Pre-Condition**：Best-Fit 管理器已在工程中声明，尚未开始登记或分配页面。
**Post-Condition**：pmm_manager 指向 best_fit_pmm_manager，并按原流程初始化其状态。
**Requirements**：仅调整要求的管理器选择，保留原初始化顺序、输出和调用关系；不增加构建配置或切换分支。
````

## 二、First-Fit 分析与思考题

负责人：2412819-李子谦。此项为源码分析与报告任务。

````text
[PROMPT]
任务：阅读 code/kern/mm/default_pmm.c，更新 report/report.md 的 4.2 First-Fit 分析和改进思考题。
操作要求：只用 apply_patch 修改该章节，不修改代码、其他章节或其他文档；以当前源码为准，不从 Git 历史复制答案。
输出要求：说明初始化、分配、释放、合并和改进代价，不把课程已有实现算作本人新增代码。

[RELY]
真实 free_area_t 包含 list_entry_t free_list 和 unsigned int nr_free；Page 的 property 记录空闲块页数，PG_property 标记块首，page_link 连接空闲链。pmm.c 将公开页面操作转交当前管理器。参考 default_pmm.c、pmm.h、memlayout.h 和 libs/list.h。

[GUARANTEE]
完成本章对以下已有函数的分析，不重新实现：
```c
static void default_init(void);
static void default_init_memmap(struct Page *base, size_t n);
static struct Page *default_alloc_pages(size_t n);
static void default_free_pages(struct Page *base, size_t n);
static size_t default_nr_free_pages(void);
```

[SPECIFICATION]
## 流程分析
Pre-Condition：已经阅读真实实现，区间和调用满足各函数的合法输入约定。
Post-Condition：解释空链和计数初始化、页面登记的标记和引用状态、首个适配选择及余块拆分、地址有序释放和相邻合并，说明各步骤的计数变化。
Case：说明恰好匹配、需要拆分、连续空间不足和前后邻块可合并的不同情况；总空闲页足够不保证连续分配成功。
## 改进思考题
Pre-Condition：已明确当前地址有序链表策略。
Post-Condition：回答搜索和插入的复杂度，分析减少搜索成本、缓解外部碎片的可选方案及索引维护代价；不能认为 Best-Fit 总是优于 First-Fit。
````

## 三、Best-Fit 连续物理页分配

负责人：2412819-李子谦。

````text
[PROMPT]
任务：从 lab2 初始框架补全 code/kern/mm/best_fit_pmm.c 的 Best-Fit 分配器。
操作要求：用 apply_patch 修改这个真实文件；保留 lab2，填入学号 2412819。不得修改 basic_check()、best_fit_check()、组长的 best_fit_and_slub_check()、公共文件或他人模块。不从 Git 历史复制旧实现，不运行构建、提交或新增其他文件。
输出要求：按下面真实接口和规格完成代码，说明设计、实际改动与未验证项。

[RELY]
阅读 default_pmm.c、pmm.h、memlayout.h、libs/list.h。真实定义为：
```c
struct Page { int ref; uint64_t flags; unsigned int property; list_entry_t page_link; };
typedef struct { list_entry_t free_list; unsigned int nr_free; } free_area_t;
```
可用页标记宏、set_page_ref、le2page 和链表接口。连续描述符对应连续物理页。原测试直接保存和恢复 free_list/nr_free，并合法分段归还页面；组长已提供 Best-Fit 与 SLUB 的组合检查函数。

[GUARANTEE]
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```
内部辅助函数和状态设为 static；管理器名称为 best_fit_pmm_manager，check 指向保留的 best_fit_and_slub_check。

[SPECIFICATION]
## best_fit_init
Pre-Condition：尚无活动分配。
Post-Condition：空闲链为空，空闲数为零。
## best_fit_init_memmap
Pre-Condition：n>0，区间合法、保留且未登记。
Post-Condition：全部 n 页进入空闲集合，引用归零，计数增加 n，区间外页面不变。
## best_fit_alloc_pages
Pre-Condition：已初始化，n>0，串行调用。
Post-Condition：选择最小可容纳块，同大小选低地址；交付恰好 n 页，余块可继续分配。无合适块返回 NULL，状态不变。
## best_fit_free_pages
Pre-Condition：归还合法持有的 n>0 个连续页，支持合法分段归还，不重复释放。
Post-Condition：计数增加 n，与前后物理相邻空闲块合并，不跨越占用页或保留区。
## best_fit_nr_free_pages / 注册
Pre-Condition：管理器已初始化。
Post-Condition：空闲计数准确，接口注册完整，兼容原测试状态保存与恢复，保留 SLUB 组合检查。
````

## 四、Buddy System 分配模块

负责人：2412819-李子谦。本提示词仅负责分配器，实现与测试分开。

````text
[PROMPT]
任务：从零创建 code/kern/mm/buddy_pmm.c、buddy_pmm.h，实现 Buddy 物理页分配器。
操作要求：用 apply_patch 创建真实文件，保留 lab2 和学号 2412819；不改 Page、pmm.c、Makefile、其他模块或测试，不从 Git 历史复制旧实现，不运行构建或提交。其他管理器由现有 tools/lab2_verify.py 在临时副本中接入。
输出要求：依据下面规格完成代码，说明取整容量、释放约定及未验证项。

[RELY]
阅读 pmm.h、memlayout.h、libs/list.h，真实结构为：
```c
struct Page { int ref; uint64_t flags; unsigned int property; list_entry_t page_link; };
```
PGSIZE=4096，pages[i] 对应物理页号 nbase+i，有效描述符数为 npage-nbase；当前物理管理窗口最多 128 MiB。可用页状态宏、page2ppn/page2pa、set_page_ref、链表接口，不使用 malloc。独立文件将提供 void buddy_check(void)。

[GUARANTEE]
```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
const struct pmm_manager buddy_pmm_manager;
```
头文件声明管理器和 buddy_check；check 指向 buddy_check，名称为 Buddy System；辅助函数和状态为 static。

[SPECIFICATION]
## buddy_init
Pre-Condition：首次使用，没有活动分配。
Post-Condition：各阶空闲集合为空，计数为零。
## buddy_init_memmap
Pre-Condition：n>0，区间合法、保留且未登记；多次登记不重叠。
Post-Condition：全部 n 页可管理，引用归零，计数增加 n；未对齐起点、非 2 次幂长度不丢页，不纳入区间外或保留页。
## buddy_alloc_pages
Pre-Condition：已初始化，单核串行调用。
Post-Condition：成功返回不小于 n 的最小 2 次幂容量，物理页号按容量对齐，与活动块不重叠，扣除实际容量。
Case：零请求、超范围、取整溢出、页面不足或无合适连续块时返回 NULL，状态不变。
## buddy_free_pages
Pre-Condition：传回原首指针及原始请求 n，分配仍有效；不支持子段或重复释放。
Post-Condition：完整归还实际容量，仅与同阶空闲的真正伙伴逐级合并，不影响其他分配。申请 3 页占用 4 页，释放传 3，回收 4 页。
Requirements：按实际物理页号判断伙伴，不能把仅相邻的块误合并；不承诺安全识别任意野指针。
## buddy_nr_free_pages / 注册
Pre-Condition：已初始化。
Post-Condition：空闲计数准确，管理器和头文件接口完整，支持独立测试及当前管理窗口。
````

## 五、Buddy 独立测试

负责人：2412819-李子谦。

````text
[PROMPT]
任务：从零创建 code/kern/mm/buddy_check.c，独立验证 Buddy 分配器。
操作要求：用 apply_patch 创建真实文件，保留 lab2 和学号 2412819；只改这个文件。不读 buddy_pmm.c 的私有实现，不改算法、框架或课程测试，不从 Git 历史复制旧测试，不运行构建或提交。
输出要求：说明覆盖的行为、为何有效、实际改动和测试局限，不把未执行的检查写成已通过。

[RELY]
真实公开接口来自 pmm.h、buddy_pmm.h：
```c
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
extern const struct pmm_manager buddy_pmm_manager;
```
可用 pages、npage、nbase、pmm_manager、PageReserved、page2ppn/page2pa、va_pa_offset、assert、cprintf。PGSIZE=4096，内核栈 8 KiB。Buddy 按最小 2 次幂容量分配，释放用原首指针和原请求；失败返回 NULL 且状态不变。

[GUARANTEE]
```c
void buddy_check(void);
```
辅助函数设为 static，大记录数组放静态存储，不新增日志或工具。

[SPECIFICATION]
## buddy_check
Pre-Condition：当前管理器是 Buddy，初始化后无其他活动分配，单核串行；标准启动空闲区至少 64 页，并存在 16 页对齐的连续区域。
Post-Condition：所有测试分配配对释放，空闲页数恢复、保留状态不变；完整重复两轮且所有断言通过后才输出 buddy_check() succeeded!。
Case 1：验证 1/2/3/4/5/8/9 页请求的取整容量、物理对齐、描述符范围和完整占用区间互不重叠。
Case 2：在每页首部、中部、末尾写标记，后续分配及释放后复查存活数据，检查误覆盖。
Case 3：检查零请求、超大请求、耗尽及碎片导致的失败，确认空闲计数和存活内容不变。
Case 4：检查真正伙伴合并、相邻非伙伴不误合并、多阶合并后再次拆分，以及非 2 次幂请求按原请求释放。
Requirements：只检查公开行为，不读取私有链表、阶数编码或私有计数；根据实际物理地址识别伙伴，不假设申请顺序决定相邻，不重置分配器掩盖泄漏。
````

### 测试修正提示词

````text
[PROMPT]
任务：修正 code/kern/mm/buddy_check.c 的描述符记录数组上限。
操作要求：仅用 apply_patch 修改这个文件，不修改算法、其他文件或测试断言，不运行构建或提交。
输出要求：说明上限计算为何错误、实际修正及未验证项；每轮打印轮次与初始空闲页数，最终成功标记保持不变。

[RELY]
KMEMSIZE=0x7e00000，只描述内核映射窗口；PHYSICAL_MEMORY_END=0x88000000，DRAM_BASE=0x80000000，PGSIZE=4096。描述符数量 npage-nbase 包括前 2 MiB 的保留页，所以 128 MiB 配置有 32768 个描述符，而 KMEMSIZE/PGSIZE 只有 32256。

[GUARANTEE]
保留 void buddy_check(void)，修正私有 BUDDY_CHECK_MAX_PAGES 及对应静态记录数组的容量。

[SPECIFICATION]
## buddy_check 的容量与输出
Pre-Condition：标准启动、描述符覆盖当前物理管理窗口，保留页也必须记录。
Post-Condition：静态记录能容纳 (PHYSICAL_MEMORY_END-DRAM_BASE)/PGSIZE 个描述符，不截断、不跳过保留状态检查；完整两轮通过后才输出成功，每轮输出轮次和初始空闲页数。
````

## 六、SLUB 对象分配模块

负责人：2412852-梁靖泽。本提示词仅负责对象分配器。

````text
[PROMPT]
**任务**：在 code/kern/mm/slub.c、slub.h 中实现页分配器之上的 SLUB 对象管理，支持不同大小对象的分配、释放、统计和整体关闭。
**操作要求**：必须修改两个实际文件，只实现本模块接口和内部辅助函数；不修改 Page、pmm.c、init.c、Makefile、课程测试或 slub_check.c，不使用 libc malloc()/free()。保留 lab2 和 2412852 标记。
**输出要求**：使用以下真实依赖实现模块，说明对象与页面的生命周期、错误处理和实际验证结果。

[RELY]
pmm.h 已提供：
```c
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
extern uint64_t va_pa_offset;
```
PGSIZE=4096；page2pa() 将 Page 描述符转成物理地址，page2pa(page)+va_pa_offset 为对应内核虚拟地址。可使用 libs/list.h、string.h 的已有接口，不访问具体页分配器的私有状态。Buddy 可能保留向上取整的页容量，释放时必须传回底层申请的原始页数。
slub.h 的现有状态码为：
```c
#define SLUB_OK 0
#define SLUB_ERR_INVALID -1
#define SLUB_ERR_DOUBLE_FREE -2
#define SLUB_ERR_UNINITIALIZED -3
```

[GUARANTEE]
必须实现并在 slub.h 保持一致声明：
```c
int slub_init(void);
void slub_shutdown(void);
void *slub_alloc(size_t size);
int slub_free(void *object);
size_t slub_nr_slabs(void);
size_t slub_nr_live_objects(void);
```
保留现有 int slub_check(void) 声明，测试实现由独立任务负责。允许内部 static 类型、状态和辅助函数，不新增公共分配接口。

[SPECIFICATION]
## slub_init
**Pre-Condition**：底层页面接口可用，处于单核串行环境。
**Post-Condition**：模块进入可分配状态；首次初始化时对象、slab 和大对象跟踪状态为空。
**Case 1**：首次初始化返回 SLUB_OK，不遗留活动分配。
**Case 2**：已初始化时重复调用返回 SLUB_OK，不破坏存活对象及其统计。

## slub_alloc
**Pre-Condition**：参数表示所需可写字节数；正常分配要求模块已初始化。
**Post-Condition**：成功返回至少容纳 size 字节、按 8 字节对齐且与其他存活对象不重叠的地址，存活对象数增加一；失败返回 NULL，状态与资源占用不产生泄漏。
**Case 1**：未初始化或 size 为零时返回 NULL，不改变状态。
**Case 2**：1 至 2048 字节请求使用 8/16/32/64/128/256/512/1024/2048 字节缓存中最小可容纳档位；同类对象可以共享 slab 页面。
**Case 3**：超过 2048 字节的对象获得足够的连续页面，保存完整回收所需信息，用户可写范围不能覆盖管理元数据。
**Case 4**：底层页面不足，或头部大小、对齐与页数计算存在溢出时返回 NULL；不得登记不完整对象。

## slub_free
**Pre-Condition**：非空参数正常情况下必须是本模块返回且仍存活的原对象指针；不支持任意地址探测。
**Post-Condition**：有效释放后对象数减少一，存储可复用或页面被回收，其他存活对象不受影响。
**Case 1**：object 为 NULL 时作为空操作返回 SLUB_OK。
**Case 2**：有效小对象释放成功；所属 slab 全空时归还页面。对尚未被回收、重用的缓存对象重复释放时返回 SLUB_ERR_DOUBLE_FREE，不破坏空闲链和统计。
**Case 3**：有效大对象释放时，将其底层页面按原申请页数完整归还。
**Case 4**：未初始化时非空释放返回 SLUB_ERR_UNINITIALIZED；可辨认的非法对象状态返回 SLUB_ERR_INVALID，不将其错误计为成功释放。
**Requirements**：不承诺检测已归还或重新分配内存上的任意悬空指针，错误返回不能破坏现有存活对象。

## slub_shutdown
**Pre-Condition**：底层分配器仍可用，关闭后调用者不再访问原对象。
**Post-Condition**：归还本模块持有的全部缓存页和大对象页，计数归零并回到未初始化状态；之后可重新初始化。
**Case 1**：存在存活小对象或大对象时，仍完成全部页面回收。
**Case 2**：未初始化时重复关闭为空操作。

## slub_nr_slabs / slub_nr_live_objects
**Pre-Condition**：无额外前置条件。
**Post-Condition**：分别返回当前缓存 slab 数与全部存活对象数；大对象计入对象数，不将其页数混作缓存 slab 数。回收和关闭后的统计与实际状态一致。
**Requirements**：本实验不要求 NUMA、per-CPU 缓存或并发锁；元数据必须与用户可写范围隔离，底层页分配和释放严格配对。
````

## 七、SLUB 独立测试

负责人：2412852-梁靖泽。

````text
[PROMPT]
**任务**：在 code/kern/mm/slub_check.c 中实现独立对象分配自检，检验尺寸边界、内容保持、复用、错误处理和页面回收。
**操作要求**：必须修改实际自检文件，只通过公开接口观察行为，不修改 slub.c、页分配器、公共启动文件、Makefile 或课程测试。现有学生组合检查入口和临时副本验证工具负责调用自检，不另改框架接入。
**输出要求**：说明覆盖场景及其检验作用，提供实际构建与运行结果；不得把编译通过、外部超时或单一成功字符串作为全部测试正确的依据。

[RELY]
slub.h 提供以下真实接口和 SLUB_OK、SLUB_ERR_DOUBLE_FREE 等状态码：
```c
int slub_init(void);
void slub_shutdown(void);
void *slub_alloc(size_t size);
int slub_free(void *object);
size_t slub_nr_slabs(void);
size_t slub_nr_live_objects(void);
```
pmm.h 提供 size_t nr_free_pages(void)，PGSIZE=4096；可使用 assert()、cprintf() 和 memset()。slab 全空会回收底层页；shutdown 也回收尚存活对象的页面，之后原指针失效。

[GUARANTEE]
必须实现：
```c
int slub_check(void);
```
允许添加本文件 static 辅助函数，不依赖 SLUB 私有结构或私有统计变量。

[SPECIFICATION]
## slub_check
**Pre-Condition**：物理页分配器可用，SLUB 无其他调用者的活动对象，空闲页足以运行测试；无并发代码改变观察到的计数。
**Post-Condition**：测试分配全部回收，存活对象和 slab 数为零，底层空闲页数恢复初值，模块能够在 shutdown 后重新初始化；成功返回 SLUB_OK 并输出 slub_check() succeeded!。
**Case 1**：零尺寸返回 NULL，释放 NULL 返回 SLUB_OK，相关统计不变。
**Case 2**：检查 1、7、8、15、24、63、64、127、128、255、256、511、512、1024、2048、2049 字节请求；每个成功对象的完整请求范围可写可读，存活对象不重叠。
**Case 3**：交错释放不同尺寸对象后，存活对象内容保持；同一缓存再次分配可正常使用，全部释放后 slab 可回收，统计归零。
**Case 4**：让同缓存另一个对象保持存活，使被释放对象的 slab 仍受模块管理，再次释放原对象应返回 SLUB_ERR_DOUBLE_FREE；随后其他对象仍可正常释放。
**Case 5**：分配超过两页的对象，写入并读回整个请求范围后释放，确认大对象页面完整回收。
**Case 6**：同时保留小对象和大对象后执行 shutdown，确认统计清零、未初始化时分配失败；重新初始化成功，最终空闲页数与测试开始一致。
**Requirements**：不能访问 shutdown 后的旧对象，不能通过修改分配器、重置底层计数或删除断言掩盖错误。测试应能在 First-Fit、Best-Fit 和 Buddy 后端下复用，按各后端公开契约使用页面。
````

## 八、QEMU 页表与 TLB 调试

负责人：2412439-崔迪。

````text
[PROMPT]
**任务**：完善 code/tools/debug_mmu.py，通过宿主机 GDB 跟踪 QEMU 4.1.1 的真实访存翻译，并用 guest GDB 核对对应内核指令；将方法、调用链和实测结果记录到 report/report.md 的调试章节。
**操作要求**：必须操作实际工具和报告文件，不修改内核入口、DTB 代码、页表常量、Makefile、课程测试或 QEMU 源码。验证内核只在临时副本中生成，仓库默认 Best-Fit 配置保持不变。
**输出要求**：保存可追溯的断点、单步、调用栈和数值结果，说明 TLB 查询位置、页表关键分支、异常信息及其影响；环境不满足时明确说明，不填入预设的成功结果。

[RELY]
已有 tools/lab2_verify.py 提供 prepare_tree(source, destination, backend, slub_check=True)，可生成独立验证副本。debug_mmu.py 使用 Python argparse、pathlib、subprocess 和 tempfile，现有命令行参数为 --qemu-source、--gdb、--port。
目标为 QEMU 4.1.1 的带符号原生构建，相关源码位置：
- target/riscv/cpu_helper.c：get_physical_address、riscv_cpu_tlb_fill。
- accel/tcg/cputlb.c：load_helper、tlb_set_page_with_attrs。
- include/exec/cpu_ldst.h：tlb_index、tlb_entry。
课程内核使用 Sv39，镜像加载在 0x80200000。128 MiB virt 配置下可跟踪 DTB 所在页的真实加载；目标地址和断点必须依据本次源码、符号及寄存器确认，host 指针不能视为固定值。

[GUARANTEE]
必须保持并完善现有 Python 接口：
```python
def line_containing(path, needle): ...
def run(command, cwd): ...
def main(): ...
```
交付可运行的调试工具及主报告中的调用路径、翻译示例、TLB 对比和排查记录。允许新增文件内部辅助函数及 GDB 命令文本，不新增内核接口。

[SPECIFICATION]
## line_containing
**Pre-Condition**：path 指向目标版本源码，needle 为期望定位的源码锚点。
**Post-Condition**：仅在匹配唯一时返回包含文件路径和行号的位置，避免对错误版本或歧义源码设置断点。
**Case 1**：唯一匹配时返回正确位置。
**Case 2**：缺失或存在多个匹配时明确报错，不任意选取行号继续验证。

## run
**Pre-Condition**：命令参数和工作目录有效，命令执行受时间边界约束。
**Post-Condition**：保留并显示标准输出与错误信息，成功返回输出文本；非零退出或超时应向上报告失败，不能丢失诊断。

## main
**Pre-Condition**：具备 RISC-V 工具链、支持目标架构的 GDB，以及 QEMU 4.1.1 源码和未剥离构建；调试端口可用。
**Post-Condition**：完成真实访存的 TLB 未命中、PTE 解析、物理地址生成和 TLB 填充核对，guest 指令数据与 host 观察一致；记录全部必要证据并清理临时文件和子进程。
**Case 1**：Sv39 数据读取时，确认访问类型、SATP、根页表地址、实际访问的 PTE、叶子权限、翻译结果和填充后的标签。遇上层大页叶子时允许提前结束遍历，不能强制假设读满三层。
**Case 2**：裸地址模式下跟踪真实访存，对比 VA/PA 关系及软件 TLB 是否仍被填充，解释 QEMU 软件缓存与真实 CPU TLB 的逻辑区别。
**Case 3**：通过独立 guest 调试运行核对访存指令、寄存器与加载数据，避免将调试器读内存触发的查询混作 guest CPU 访存。
**Case 4**：缺少符号、断点未命中、翻译或数据不符时明确失败；ASLR 权限告警与优化变量信息需结合后续检查判断，不能仅凭告警判定内核错误。
**Requirements**：对 SATP 等寄存器按正确位宽解释；host 与 guest 地址层次分开记录；只在真实断点和数据检查成功后输出成功标记，不硬编码预期地址来替代观察。
````

## 九、Challenge：未知硬件的内存范围获取方案

负责人：2412439-崔迪。此项为选做分析。

````text
[PROMPT]
**任务**：在 report/report.md 的 Challenge 章节说明操作系统如何在未知硬件上取得可用物理内存范围，并联系本实验 DTB 的产生、传递和解析过程。
**操作要求**：实际更新报告对应章节，依据已有代码分析，不修改 DTB 解析、启动参数或内存初始化框架，不将设计建议描述为已经实现的功能。
**输出要求**：交代内存描述的提供者、启动传递过程、内核使用方式和必要的范围排除，区分当前实现与完整平台需求。

[RELY]
QEMU 4.1.1 的 hw/riscv/virt.c:create_fdt() 生成 memory 节点；本工程入口保存 DTB 地址，code/kern/driver/dtb.c 进行解析，dtb.h 已提供：
```c
void dtb_init(void);
uint64_t get_memory_base(void);
uint64_t get_memory_size(void);
```
page_init() 消费解析后的单段 RAM 信息，并受 PHYSICAL_MEMORY_END 及内核、描述符占用限制。这些函数均为已有接口，本任务只分析其关系。

[GUARANTEE]
完成报告中的内存范围获取方案，覆盖硬件描述来源、启动参数传递、RAM 与保留区区分，以及多段 RAM 的扩展需求。此任务不新增或修改 C 接口，不生成替代实现。

[SPECIFICATION]
## 内存范围获取方案
**Pre-Condition**：内核无法事先写死全部 RAM 容量，启动环境能够提供硬件内存描述。
**Post-Condition**：方案说明如何从可信启动信息取得范围，并指出登记空闲页前必须排除固件、内核、描述符和设备等占用；不将硬件描述容量直接当作可分配容量。
**Case 1**：单段 DTB RAM 描述有效时，联系现有 getter 和 page_init() 解释当前实验的数据流。
**Case 2**：描述缺失或非法时，说明需要明确失败或采用经过验证的平台机制，不能猜测内存上界并直接登记。
**Case 3**：存在多段 RAM、固件保留区或内存映射设备时，说明完整实现需要额外解析和区间排除；明确这些能力未由本实验单段 getter 完整提供。
**Requirements**：区分方案分析与已完成代码；不把探测 RAM、限定管理范围和计算空闲页混为同一概念。
````

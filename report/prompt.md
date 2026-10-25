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
**任务**：阅读 code/kern/mm/default_pmm.c，在 report/report.md 的 First-Fit 章节说明初始化、连续页分配、回收和合并的行为，并回答算法是否存在进一步改进空间。
**操作要求**：实际更新报告对应章节，保留其他实验内容；不修改 First-Fit 源码、课程框架或测试，不将已有算法列为本组新增实现。
**输出要求**：依据真实源码解释数据结构、不变量和边界行为，给出改进思路及其代价，不用抽象算法描述代替对工程代码的分析。

[RELY]
default_pmm.c 使用 free_area_t，其定义来自 memlayout.h：
```c
typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;
```
空闲链表节点为 struct Page 的 page_link，块首 property 表示连续空闲页数，PG_property 标识空闲块首。pmm.c 的 alloc_pages()/free_pages()/nr_free_pages() 转发给当前管理器。连续描述符对应连续物理页，但描述符本身不等同于物理页。

[GUARANTEE]
必须完成报告中的 First-Fit 功能分析及改进问题回答，覆盖以下既有接口；这些接口仅供分析，不重新实现：
```c
static void default_init(void);
static void default_init_memmap(struct Page *base, size_t n);
static struct Page *default_alloc_pages(size_t n);
static void default_free_pages(struct Page *base, size_t n);
static size_t default_nr_free_pages(void);
```

[SPECIFICATION]
## default_init / default_init_memmap 的分析
**Pre-Condition**：已阅读真实实现，登记区间合法且尚未被分配器管理。
**Post-Condition**：报告说明空链与计数的初始状态、区间登记后的标记和引用状态、块首长度及空闲计数含义。

## default_alloc_pages 的分析
**Pre-Condition**：分配器已初始化，调用者请求 n > 0 个连续页。
**Post-Condition**：报告解释按地址顺序选择首个可容纳块、整块交付与余块保留，以及计数变化。
**Case 1**：存在合适连续块时，说明恰好匹配与需要拆分的区别。
**Case 2**：不存在足够大的连续块时，说明返回 NULL 的原因，区分总空闲页数与最大连续块长度。

## default_free_pages / default_nr_free_pages 的分析
**Pre-Condition**：归还的是合法持有且尚未释放的连续页，n > 0。
**Post-Condition**：报告解释归还页状态、按地址登记、前后相邻块合并和准确空闲计数；合并不能跨过占用页或保留区。

## 改进问题回答
**Pre-Condition**：已经明确当前线性链表策略及其复杂度。
**Post-Condition**：至少分析搜索成本和外部碎片，提出分级索引、平衡树、位图或 Next-Fit 等可选方向，并说明索引维护成本、空间开销及请求分布的影响。
**Requirements**：不能将 Best-Fit 描述为所有场景下都更优；应区分分配策略、物理连续性和虚拟地址翻译。
````

## 三、Best-Fit 连续物理页分配

负责人：2412819-李子谦。

````text
[PROMPT]
**任务**：在 code/kern/mm/best_fit_pmm.c 的学生实现区域完成 Best-Fit 页分配模块，提供完整的物理页管理器注册，使原课程自检能够通过。
**操作要求**：必须修改实际文件；保留已有正确实现及 lab2、2412819 标记。不得修改 basic_check()、best_fit_check()、公共头文件、Makefile 或其他模块。已存在的 SLUB 组合检查接入保持原样。
**输出要求**：以真实接口和测试契约为准，说明修改内容、原测试兼容性及实际运行结果；不通过删改断言或评分脚本获取通过结果。

[RELY]
参考 default_pmm.c、pmm.h、memlayout.h 和 libs/list.h。真实管理状态类型为：
```c
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};
typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;
```
pmm_manager 的接口成员为 name、init、init_memmap、alloc_pages、free_pages、nr_free_pages、check，签名以 pmm.h 为准。可使用现有页标记宏、set_page_ref()、le2page() 和链表操作。原测试直接保存并恢复 free_list、nr_free，也会合法地分段归还页面；实现必须兼容这一状态约定。

[GUARANTEE]
必须实现并注册：
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```
允许增加模块内部 static 辅助函数和状态；不更改公开接口或原有测试函数。

[SPECIFICATION]
## best_fit_init
**Pre-Condition**：尚无活动分配，管理器即将开始登记空闲区间。
**Post-Condition**：空闲集合为空，空闲页计数为零，后续区间登记可正常进行。

## best_fit_init_memmap
**Pre-Condition**：n > 0，区间位于描述符数组内，各页仍为保留状态且未重复登记。
**Post-Condition**：这 n 页全部进入空闲集合，引用计数归零，管理标记和块长度一致，空闲计数增加 n；区间外状态不受影响。

## best_fit_alloc_pages
**Pre-Condition**：管理器已初始化，n > 0，串行调用。
**Post-Condition**：成功时交付恰好 n 个连续页，与其他活动分配不重叠，计数扣除 n；失败返回 NULL，状态不变。
**Case 1**：存在多个候选块时，选择容量最小的可容纳块，同大小选低地址块。
**Case 2**：候选块容量恰好为 n 时完整交付；容量大于 n 时，剩余页面仍可分配，相关块首标记、长度和链表关系有效。
**Case 3**：总空闲页不足，或因外部碎片不存在足够大的连续块时，返回 NULL，不破坏原状态。

## best_fit_free_pages
**Pre-Condition**：base 起的 n > 0 个连续页为合法持有页；支持合法分段归还，但不允许重复释放或释放保留页。
**Post-Condition**：计数准确增加 n；归还区间与前后物理相邻空闲块能够合并，其他活动分配保持有效。
**Case 1**：没有相邻空闲块时，区间作为独立空闲块登记。
**Case 2**：仅前侧、仅后侧或两侧存在物理相邻空闲块时，形成对应的完整连续空闲块，计数不重复增加。

## best_fit_nr_free_pages / best_fit_pmm_manager
**Pre-Condition**：管理器状态已初始化。
**Post-Condition**：返回值准确反映全部空闲页；注册结构的各函数指针有效，原课程检查通过既有入口执行。
**Requirements**：空闲块首标记、块长度、链表和计数保持一致；不得修改原测试保存/恢复状态的约定。原有 SLUB 组合回调不得因本模块补全被删除。
````

## 四、Buddy System 分配模块

负责人：2412819-李子谦。本提示词仅负责分配器，实现与测试分开。

````text
[PROMPT]
**任务**：在 code/kern/mm/buddy_pmm.c、buddy_pmm.h 中实现 Buddy 物理页分配模块，通过 pmm_manager 接口提供分配、释放和空闲页统计。
**操作要求**：必须修改这两个实际文件，保留 lab2 和 2412819 标记；只使用现有框架接口，不修改 Page 布局、pmm.c、Makefile、其他分配器或 buddy_check.c。通过已有临时副本验证入口完成接入验证，不在课程框架内增加后端切换逻辑。
**输出要求**：说明接口、请求容量取整和释放配对约定，报告实际编译/运行结果与限制。

[RELY]
pmm.h 提供 struct pmm_manager、页状态和页面转换接口。PGSIZE=4096，DRAM_BASE=0x80000000，PHYSICAL_MEMORY_END=0x88000000，当前框架管理窗口不超过 128 MiB。
```c
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
```
有效描述符数量为 npage - nbase；pages[i] 对应物理页号 nbase+i。可使用 page2ppn()、page2pa()、页状态宏、set_page_ref() 及现有链表接口，不依赖 malloc()。独立测试在 buddy_check.c 提供 void buddy_check(void)。

[GUARANTEE]
必须实现：
```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
const struct pmm_manager buddy_pmm_manager;
```
buddy_pmm.h 声明 buddy_pmm_manager 和 buddy_check()；管理器 check 入口连接独立测试。允许添加模块内部 static 状态和辅助函数。

[SPECIFICATION]
## buddy_init
**Pre-Condition**：首次初始化，尚无活动分配。
**Post-Condition**：各阶空闲集合为空，空闲页计数为零，支持后续区间登记。

## buddy_init_memmap
**Pre-Condition**：n > 0，区间合法、仍为保留状态、未登记；多次登记的区间不重叠。
**Post-Condition**：全部 n 页可管理，引用状态正确，计数增加 n；不丢失合法尾页，不纳入区间外或保留页面。
**Case 1**：区间起点与长度恰好满足相应阶对齐时，登记后容量完整可用。
**Case 2**：起点未按大块对齐或长度不是 2 的幂时，仍完整覆盖原合法区间，不因取整丢页。

## buddy_alloc_pages
**Pre-Condition**：管理器已初始化，串行调用。
**Post-Condition**：成功时返回不小于 n 的最小 2 次幂容量，物理页号按实际容量对齐，与其他活动块不重叠，计数扣除实际容量。
**Case 1**：n 是 2 的幂且有足够连续容量时，交付对应容量。
**Case 2**：n 不是 2 的幂时，交付最小取整容量，并保留正确释放该分配所需的信息。
**Case 3**：n 为零、超过管理范围、取整溢出、页面不足或不存在合适连续块时，返回 NULL；空闲计数及其他存活数据保持不变。

## buddy_free_pages
**Pre-Condition**：base 是该次分配的原首指针，n 是原始请求；分配仍有效，不支持分段或重复释放。
**Post-Condition**：完整归还该次分配的实际容量，准确恢复计数；其他分配不受影响，满足条件的伙伴能够逐级合并。
**Case 1**：申请 3 页实际占用 4 页时，按原请求 3 释放必须归还全部 4 页。
**Case 2**：真正的同阶空闲伙伴可以合并；仅地址相邻但非伙伴、已分配块或区间外页面不能误合并。
**Requirements**：伙伴关系基于实际物理页号与块容量，不能使用描述符指针或虚拟地址直接判断。违反释放前置条件可断言拒绝，不承诺安全识别任意野指针。

## buddy_nr_free_pages / buddy_pmm_manager
**Pre-Condition**：管理器已初始化。
**Post-Condition**：空闲数准确，注册完整，能够通过公共页面接口及独立检查入口使用。
**Requirements**：覆盖当前窗口所需的阶范围；本实验限定单核串行，不要求实现并发分配或 NUMA。
````

## 五、Buddy 独立测试

负责人：2412819-李子谦。

````text
[PROMPT]
**任务**：在 code/kern/mm/buddy_check.c 中实现 Buddy 的独立自检，通过公开契约验证容量、对齐、数据保持、伙伴合并及资源回收。
**操作要求**：必须修改实际自检文件，只修改该文件及其内部 static 辅助函数；不改 Buddy 算法、公共框架或课程测试，不读取分配器私有链表、阶数编码或私有计数来构造通过结果。
**输出要求**：说明每类测试检查的行为及其有效性，给出真实执行结果和覆盖限制；失败不能被忽略或转换为成功。

[RELY]
真实公开接口为：
```c
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
extern const struct pmm_manager buddy_pmm_manager;
extern const struct pmm_manager *pmm_manager;
```
可用 pages、npage、nbase、PageReserved()、page2ppn()、page2pa()、va_pa_offset、assert() 和 cprintf()。Buddy 占用最小 2 次幂容量，释放传回原首指针与原始请求。PGSIZE=4096，内核栈为两页；物理页的内核虚拟地址由 page2pa(page)+va_pa_offset 得到。

[GUARANTEE]
必须实现：
```c
void buddy_check(void);
```
辅助函数设为 static；大规模记录放静态存储，不放入有限内核栈。

[SPECIFICATION]
## buddy_check
**Pre-Condition**：当前管理器为 Buddy，初始化和空闲区登记完成；单核串行、无其他活动分配。标准启动空闲区至少有 64 页，并存在 16 页对齐的连续区域，满足合并测试的连续性前提。
**Post-Condition**：全部测试分配均被配对释放，空闲页数恢复初值，保留页不变，分配器仍可继续使用；只有所有断言通过后才输出 buddy_check() succeeded!。
**Case 1**：检查 1/2/3/4/5/8/9 页请求的实际容量扣减、物理对齐和整个占用区间不重叠；在页面首部、中部、末尾写入标记并反复核对存活数据。
**Case 2**：检查零请求、超大请求、容量不足和耗尽情况下的失败，确认返回 NULL、计数不变且已有分配内容完整。
**Case 3**：构造有足够总空闲页但无足够连续块的碎片场景；检查真正伙伴可合并、相邻非伙伴不可误合并，以及多阶合并后能够再次拆分。
**Case 4**：以原请求释放非 2 次幂分配，使用不同释放顺序检验状态恢复；完整测试重复两轮，发现陈旧元数据和链表破坏。
**Requirements**：依据返回的物理地址识别伙伴，不能假设申请顺序决定物理相邻。每个阶段保留并检查存活数据；不能重置管理器或修改统计来掩盖泄漏。初始总空闲数不能替代连续区域前提。
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

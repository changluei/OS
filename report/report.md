# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| 实验名称 | Lab 2：物理内存和页表 |
| 小组成员 | 2412439-崔迪、2412819-李子谦、2412852-梁靖泽 |
| 完成日期 | 2026-10-25 |

### 小组分工

| 成员 | 实验任务 | 报告内容 |
|------|----------|----------|
| 2412439-崔迪 | 物理内存初始化、SLUB 公共检查接入、GDB 页表与 TLB 调试、集成验证 | 实验目的与环境、整体逻辑、初始化与调试分析、测试结果及报告整合 |
| 2412819-李子谦 | First-Fit 分析、Best-Fit 实现、Buddy 实现及独立测试 | 页分配算法的规格、提示词、实现过程、测试分析与思考题 |
| 2412852-梁靖泽 | SLUB 对象分配及独立测试 | 对象分配的规格、提示词、问题修正、测试分析与实验总结 |

## 一、实验目的

1. 理解从硬件内存描述到物理页管理的初始化过程，掌握页描述符、保留区和空闲区的组织方法。
2. 理解连续物理页分配策略，实现 Best-Fit 和 Buddy System，分析分配、拆分、回收与合并的正确性条件。
3. 在页分配器之上实现 SLUB 对象分配，理解页级管理与对象级管理的关系。
4. 通过 GDB 和 QEMU 源码跟踪 Sv39 地址翻译及软件 TLB 的查询、未命中和填充过程。

## 二、实验环境

### 2.1 开发与运行环境

| 成员 | 宿主机与 Linux 环境 | RISC-V GCC / binutils | QEMU / GDB |
|------|---------------------|----------------------|------------|
| 崔迪 | macOS ARM64；Docker Ubuntu 22.04 / aarch64 | 10.2.0 / 2.35.1 | 4.1.1 / gdb-multiarch 12.1 |
| 李子谦 | Windows WSL2；Ubuntu 24.04.4 LTS / x86_64 | 10.2.0 / 2.35 | 4.1.1 / riscv64-unknown-elf-gdb 10.1 |
| 梁靖泽 | Windows WSL2；Ubuntu 22.04 / x86_64 | 15.1.0 / 2.45 | 4.1.1 / riscv64-unknown-elf-gdb 16.3.90.20250610-git |

本实验内核的虚拟链接基址为 `0xffffffffc0200000`，镜像加载物理地址为 `0x80200000`，页大小为 4096 字节。

### 2.2 AI 编程工具

| 成员 | AI 编程工具 | 底层模型 | 
|------|-------------|----------|
| 崔迪 | Claude Code | deepseek-v4-flash | 
| 李子谦 | Claude Code | deepseek-v4-flash  |
| 梁靖泽 | Claude Code | deepseek-v4-flash  |

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本实验围绕物理内存的发现、登记、分配和回收展开。启动汇编建立初始页表并开启 Sv39，使内核能够在虚拟地址空间中运行。DTB 解析提供 RAM 的基址和容量，`page_init()` 据此安排 `Page` 描述符，排除固件、内核和管理元数据占用的区域，再将完整空闲页交给物理页分配器。

`pmm_manager` 通过统一接口封装不同的页分配算法。First-Fit 与 Best-Fit 使用连续空闲块链表，区别在于候选块的选择策略；Buddy 按阶组织空闲块，通过拆分和伙伴合并管理连续页。SLUB 位于这些页分配器之上，将页面进一步划分为不同大小的对象，并在对象全部释放后归还页面。

### 3.2 功能的逐步实现

1. **建立启动映射并读取硬件信息。** `entry.S` 保存启动参数并设置 SATP，C 入口清理 `.bss`、解析 DTB，为后续内存管理提供地址映射与 RAM 范围。
2. **完成物理页初始化。** `pmm_init()` 初始化管理器，`page_init()` 建立描述符、保留占用页，并通过 `init_memmap()` 登记空闲区。只有此步骤完成后，分配器才有可用页面。
3. **实现连续物理页分配。** 通过 `alloc_pages()`、`free_pages()` 和 `nr_free_pages()` 验证 Best-Fit 与 Buddy 的分配、回收和资源守恒。
4. **实现对象分配。** SLUB 使用上述页面接口建立对象缓存，验证页内切分、对象复用和页面回收。
5. **验证地址翻译与整体行为。** 通过内核 GDB 检查页状态，通过宿主机 GDB 跟踪 QEMU 中真实访存的页表解析和 TLB 填充，再运行课程评分及组合测试。

## 四、实验内容与实现

我们先完成物理页初始化，再分析 First-Fit，并实现 Best-Fit、Buddy 与 SLUB。下面分别说明设计依据、实现过程和验证结果。按练习要求，编程任务附相应提示词；First-Fit 与选做 Challenge 直接回答思考题，GDB 部分记录源码观察、单步结果和 AI 辅助分析过程。

### 4.1 功能模块：物理内存探测与页描述符初始化

**负责人：** 2412439-崔迪。

#### 功能与实现

我们在 `code/kern/mm/pmm.c` 中完成了 `page_init()`。该函数不接收参数，通过 DTB getter 和链接符号取得输入，建立 `pages`、`npage`、`va_pa_offset`，并把可用的完整页面交给当前分配器。

```c
static void page_init(void);
```

首先需要区分硬件报告的 RAM、框架能管理的范围和最终可分配的空闲页。我们的实现检查 RAM 基址、容量及范围加法是否有效，将管理末端限制在 `PHYSICAL_MEMORY_END` 内，并舍去不足一页的尾部。若范围不能覆盖内核或容纳描述符，初始化以 `panic()` 结束，而不是继续登记无效页面。

描述符数组放在内核 `end` 之后并向上按页对齐。由于 `end` 是虚拟地址，我们先使用固定映射关系核算其物理占用。`npage` 是物理页号的排他上界，实际数组长度为 `npage - nbase`，其中 `pages[i]` 对应页号 `nbase+i`。如果直接按 `npage` 计算数组长度，就会把 RAM 起始页号之前的空间也算入管理元数据。

我们先将描述符置为保留状态，再把排除固件、内核和描述符数组后的空闲区登记给分配器。这样，管理结构本身占用的页面不会被再次分配。描述符末端向上对齐后若已无完整页面，则不调用 `init_memmap()`。

<a id="initialization-prompt"></a>

#### 最终提示词

编写规格时，我们重点约束地址层次、描述符数量、占用区排除和异常范围，并用前后置条件明确初始化成功后的状态。管理器选择由公共接入统一处理；Best-Fit 完成后按练习要求启用。

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

#### 实现与调试过程

完成初始化后，我们先用宿主机用例检查范围计算，再在 RISC-V 内核中核对保留页、空闲数和启动状态。验证中，GDB 将 SATP 解释为有符号数，使 MODE 判断出现错误；将读取值规范为 64 位无符号数后，字段检查与真实寄存器一致。这里修正的是验证脚本的数值解释，没有修改内核 SATP。

容器缺少 Python 3、C 头文件及 `bc`，补齐依赖后才完成全部检查。另一个问题是失败输出在清理临时文件时丢失，我们调整为先保留诊断再清理。早期为了独立测试增加过构建配置，最终已恢复课程 `Makefile`，将后端选择移到临时副本。

这部分实现使我们认识到：物理页管理既要计算可用容量，也要核算管理元数据自身的占用；对齐后的地址合法并不等于该页可以分配。

#### 验证结果

宿主机测试编译真实 `page_init()` 函数体，仅模拟 DTB、控制台及空闲区注册接口，并替换链接器 `end` 声明。12 个用例全部通过，覆盖正常范围、未对齐末端、管理上限、无完整空闲页、零范围、加法溢出、未覆盖内核及描述符空间不足。

真实内核另在 64、128、256 MiB RAM 下完成启动与 First-Fit 自检。GDB 确认保留页和空闲数正确；128 MiB 的独立初始化配置中初始空闲页为 31929 页。该数值依赖内核及元数据布局，不与其他构建配置的空闲页数混用。完整记录见 [初始化与页表状态日志](logs/20261025-initialization.txt)。

### 4.2 练习：First-Fit 连续物理内存分配算法分析

**负责人：** 2412819-李子谦。

#### 分析对象

分析课程提供的 `code/kern/mm/default_pmm.c`，说明页面登记、分配、释放和合并的行为。
**核心函数：**

```c
static void default_init(void);
static void default_init_memmap(struct Page *base, size_t n);
static struct Page *default_alloc_pages(size_t n);
static void default_free_pages(struct Page *base, size_t n);
static size_t default_nr_free_pages(void);
```

#### 练习分析与问题回答

`pmm.c` 的公开 `alloc_pages()`、`free_pages()` 和 `nr_free_pages()` 通过 `pmm_manager` 转发给当前管理器，因此只有选择 First-Fit 管理器时，公开接口才调用以下 `default_*` 实现。

**数据结构与有效状态。** `static free_area_t free_area` 包含链表头 `list_entry_t free_list` 和空闲页总数 `unsigned int nr_free`。链表按地址递增，仅把每个空闲块首个 `Page` 的 `page_link` 接入链中；块首的 `PG_property` 有效时，`property` 才表示该块连续空闲页数。空闲块长度之和等于 `nr_free`。`Page` 是页描述符而非物理页内容，连续描述符对应连续物理页；非块首页的 `PG_property` 为零，并不能据此断言它已经分配。

**初始化与页面登记。** `default_init()` 用 `list_init(&free_list)` 使双向循环链表头的前后指针都指向自身，并将 `nr_free` 设为零；这一步还没有登记物理页。`default_init_memmap(base, n)` 要求 `n > 0`、区间合法且未登记，逐页断言 `PageReserved(p)`，清零 `flags`、`property` 和引用计数。随后只在 `base` 设置 `property = n` 和 `PG_property`，把块首按地址插入链表，并使 `nr_free` 增加 `n`。当前登记函数没有相邻合并步骤，不能把释放路径的合并行为写成初始化时也会执行。

**首个适配分配。** `default_alloc_pages(n)` 先断言 `n > 0`；若 `n > nr_free`，直接返回 `NULL`。否则从链表头顺序查找，遇到首个 `property >= n` 的块即停止，因此选择的是低地址方向的首个适配块，而非最小适配块。找到后保存原节点的前驱并摘链：恰好匹配时不建立余块；块较大时，交付块首起的前 `n` 页，以 `page + n` 为新块首，令其长度为原长度减 `n`、设置 `PG_property`，再插回原前驱之后，保持地址有序。两种成功情况都只让 `nr_free` 减少 `n`，并清除交付块首的 `PG_property`；源码没有将交付页原有的 `property` 数值清零，也没有逐页重新设置引用，不能将残留数值误认为仍有效的空闲块长度。

若扫描完仍无合适块，则返回 `NULL`，链表、标记及计数均不改变。例如不相邻空闲块分别为 2 页和 3 页，虽然 `nr_free = 5`，申请 4 个连续页仍然失败。按地址排列的块长度为 8、5、12 页时，申请 4 页会从第一块 8 页中拆出 4 页，而不是选择后面的 5 页块。

**页面释放与相邻合并。** `default_free_pages(base, n)` 的前提是归还合法持有、尚未释放的连续 `n > 0` 页；源码逐页断言既非保留页也非空闲块首，清零 `flags` 和引用计数，再在 `base` 写入 `property = n`、设置 `PG_property`。这些断言不是完整的所有权或重复释放检测，合法调用仍由调用者保证。函数按地址插入新块，`nr_free` 只增加本次归还的 `n`，不会因合并再次增加。

插入后先检查前驱：若 `p + p->property == base`，则将本次块长度加到前驱，清除旧 `base` 的块首标记并摘链，再令 `base = p`。然后用更新后的块首检查后继：若 `base + base->property == p`，就累加后继长度、清除后继块首标记并摘链。由此覆盖不合并、只向前合并、只向后合并和同时连接前后块的情况。例如前块 2 页、本次归还 1 页、后块 3 页且三者物理连续时，最终保留一个 6 页块，但空闲计数只增加 1。地址有序使物理相邻块只能出现在链表邻居中，等式判断确保不会跨过占用页或保留区；合并不会搬移物理页。源码每侧只检查一个邻块，并非循环合并任意多个相邻登记块。

**空闲统计。** `default_nr_free_pages()` 直接返回 `nr_free`，单位是页而非字节，不扫描链表。初始化置零、登记和归还各增加 `n`、成功分配减少 `n`、合并不改变总数，共同维持计数准确。

**思考题：First-Fit 是否有进一步的改进空间？**

有，但需要区分搜索成本、碎片和实现维护成本。设空闲块数为 F，一次首个适配搜索和一次地址有序插入的最坏复杂度均为 O(F)；找到位置后的链表摘挂、余块拆分以及前后各一次合并均为 O(1)。考虑逐页状态处理，登记和释放整体为 O(n + F)，分配最坏为 O(F)，空闲计数查询为 O(1)。

- **减少重复扫描：** Next-Fit 从上次位置继续寻找，可能改善特定请求序列的平均搜索开销，但最坏仍为 O(F)，且会改变候选选择及碎片分布。新增游标还必须在摘链、拆分、合并和重置时同步维护，不能指向已失效节点。
- **建立索引：** 若必须保留按地址的 First-Fit 语义，可用按地址平衡树，并在子树保存最大块长度，将查找首个适配块和插入降为 O(log F)；代价是旋转、拆分、合并时同步更新树节点及最大长度。按大小分级的空闲链、大小有序树或非空桶位图也可加快容量定位，但通常会改变放置策略；为了快速寻找物理邻块，还需保留地址索引或增加边界元数据。拆分和合并必须同时维护各索引，并增加元数据空间，不能只计算查询的收益。单纯使用逐页位图也不保证任意长度连续段的查找变为常数时间。
- **缓解外部碎片：** 当前源码已经在释放时合并相邻块，这是课程已有机制，不是本人的新增优化；它不能把被占用页隔开的空闲块合起来。可以按请求大小和寿命调整放置策略，或采用伙伴系统以规则拆分、合并换取更简单的管理，但后者按阶取整可能引入内部碎片。Best-Fit 在上述 8、5、12 页例子中会选择 5 页块，眼前余块更小，却也可能积累难利用的小洞；线性链上的完整候选搜索仍为 O(F)，并不保证整体速度或长期碎片优于 First-Fit。

因此应结合请求分布测量扫描长度、最大连续块、失败率和元数据成本，再决定是否值得引入索引或改变策略。以上都是可选改进分析，本节未实际实现；放置策略只决定选择哪段空闲物理页，不改变页大小、物理连续性要求或虚拟地址翻译机制。

### 4.3 功能模块：Best-Fit 连续物理页分配

**负责人：** 2412819-李子谦。

#### 功能与实现

**实现文件：** `code/kern/mm/best_fit_pmm.c`。原有 `best_fit_pmm.h` 已提供所需声明。 完成模块后，我们在 `pmm.c` 的 `init_pmm_manager()` 中选择 `best_fit_pmm_manager`，使启动自检实际运行 Best-Fit，而不是仍然检查 First-Fit。

**核心函数与注册结构：**

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```

**功能说明：**

Best-Fit 保留地址有序空闲链，以便在释放时检查前后邻块；分配时扫描候选块，选择能够满足请求的最小块，同大小时选择低地址块。

- 初始化与区间登记维护保留标记、引用计数、块长度和空闲计数。
- 成功分配仅交付请求的 n 页，余块重新登记；无合适连续块时返回 `NULL`，分配器状态保持不变。
- 释放支持课程测试使用的合法分段归还，只合并物理相邻的空闲块，不跨越占用页或保留区。
- 通过 `struct pmm_manager` 注册初始化、登记、分配、释放、计数和检查接口；课程原有 `basic_check()`、`best_fit_check()` 均保持不变。

公共检查通过新增的 `static void best_fit_and_slub_check(void)` 组合原 Best-Fit 检查与 SLUB 自检。

<a id="best-fit-prompt"></a>

#### 最终提示词

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

#### 实现过程
我们保留原测试的状态保存、恢复及合法分段释放约定。初始化只登记当前传入区间；释放按地址插入并循环合并物理相邻块。

##### 编译与运行验证

在隔离副本中统一编译，运行课程评分和不同 RAM 下的 Best-Fit、SLUB 组合检查；当前版本的命令和输出见 [测试记录](logs/2412819-tests.txt)。

**最终结果：** 重新生成的实现编译通过，四种 RAM 的 Best-Fit 与 SLUB 组合检查通过，课程 page init、best fit 均 OK，总分 **20/20**。未修改原课程测试，未发生需要再次修改 Best-Fit 实现的运行失败。

我们用地址有序链支持邻块合并，用完整候选比较保证最小适配，并在大小相同时选择低地址。最小适配与相邻合并解决的是两个不同问题，需要同时维护正确的状态。

#### 问题回答

**思考题：Best-Fit 是否有进一步的改进空间？**

当前最小适配查找仍为 O(F)。可按块大小建立平衡树、分级链表或位图索引，但需要同时维护地址邻接信息以支持回收合并。Best-Fit 最小化的是单次分配余量，并不保证全局外部碎片最少；较小余块能否再次利用取决于后续请求分布。

### 4.4 功能模块：Buddy System 伙伴分配器

**负责人：** 2412819-李子谦。

#### 功能与实现

**实现文件：** `code/kern/mm/buddy_pmm.c`、`buddy_pmm.h`；独立测试位于 `buddy_check.c`。

**核心函数与注册结构：**

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
const struct pmm_manager buddy_pmm_manager;
void buddy_check(void);
```

**功能说明：**

Buddy 维护 0–15 阶空闲链，覆盖当前框架最多 128 MiB 的管理窗口。登记空闲区间时，依据物理页号对齐将区间拆成若干 2 的幂块，保证未对齐起点和非 2 次幂长度不会造成丢页。

分配 n 页时，将容量向上取整为最小的 2 次幂，从对应阶或更高阶取得空闲块并逐级拆分，空闲计数扣除实际容量。释放时依据调用者传回的原请求重新计算容量；例如申请 3 页实际占用 4 页，释放仍传 3，完整归还 4 页。本组实现不另存原请求，调用者必须遵守整块配对释放的前置条件，不保证识别所有错误的释放参数。

伙伴关系依据物理页号与块容量异或确定。仅同阶、空闲且属于管理范围内的真正伙伴能够合并，不能仅凭地址相邻判断。释放必须使用原分配首指针和原始请求，不支持分段或重复释放；零请求、超范围请求和无法满足的请求返回 `NULL`，且不改变状态。

#### 最终提示词

<a id="buddy-implementation-prompt"></a>

##### 分配器实现提示词

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

<a id="buddy-test-prompt"></a>

##### 独立测试提示词

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

##### 测试修正提示词

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

#### 实现迭代过程

##### 修正静态记录数组上限

**遇到的问题：** 在构建前静态复核发现，测试以 KMEMSIZE/PGSIZE 作为描述符记录容量，只能覆盖 32256 项；而 128 MiB RAM 的描述符包含前 2 MiB 保留区，共有 32768 项，原上限不足。

我们通过上面的测试修正提示词，将上限改为 (PHYSICAL_MEMORY_END-DRAM_BASE)/PGSIZE 计算上限。保留完整描述符和保留状态检查，没有截断数组范围或放宽断言；32 KiB 主记录数组使用静态存储，不占内核栈。每轮输出轮次与初始空闲页数。

**验证方式：** 修正后统一编译，运行四种 RAM 下的两轮 Buddy 自检及 SLUB 组合检查，结果见第五章和 [测试记录](logs/2412819-tests.txt)。

**最终结果：** 64、70、128、256 MiB 配置均完成两轮 Buddy 自检及 SLUB 检查，空闲计数恢复，验证命令正常返回；我们发现的问题是上述静态容量上限错误，修正后未发生运行时断言失败。

这一问题说明内核映射窗口不能替代物理描述符覆盖范围。我们同时检查实际物理页号、存活数据与资源恢复，避免仅凭空闲页总数判断算法正确。

#### 测试覆盖与结果分析

| 核心行为或边界 | 测试方法及检验作用 |
|----------------|--------------------|
| 容量取整、对齐与互斥 | 申请 1/2/3/4/5/8/9 页，核对实际容量、物理对齐及整个分配区间是否重叠 |
| 内容保持 | 在各页首部、中部和末尾写入标记，后续分配与释放后检查存活块，发现误覆盖或误释放 |
| 分配失败 | 检查零请求、超大请求、耗尽及碎片场景，确认失败不改变空闲计数及存活数据 |
| 伙伴合并与再拆分 | 依据实际物理地址识别伙伴，检查真正伙伴合并、相邻非伙伴不误合并及多阶合并后再分配 |
| 释放配对与资源恢复 | 使用原请求释放非 2 次幂分配，按不同顺序释放，并重复运行整套检查 |

在 WSL2 中对本组重新生成的版本的实际组合结果如下，完整输出见 [李子谦测试记录](logs/2412819-tests.txt)：

| QEMU RAM | Buddy 初始空闲页数 | 结果 |
|----------|--------------------|------|
| 64 MiB | 15695 | 两轮 Buddy 自检及 SLUB 检查通过 |
| 70 MiB | 17216 | 两轮 Buddy 自检及 SLUB 检查通过 |
| 128 MiB | 31919 | 两轮 Buddy 自检及 SLUB 检查通过 |
| 256 MiB | 31919 | 两轮 Buddy 自检及 SLUB 检查通过，管理末端仍为 `0x88000000` |

测试通过公开接口检查资源守恒和可观察行为，不读取私有链表或阶数编码。当前验证未覆盖并发、非法释放、全部高阶组合及每页全部字节。

### 4.5 功能模块：SLUB 内存对象分配

**实现负责人：** 2412852-梁靖泽；**公共检查接入与复验：** 2412439-崔迪。

#### 功能与实现

**实现文件：** `code/kern/mm/slub.c`、`slub.h`；独立测试位于 `slub_check.c`。

**核心函数：**

```c
int slub_init(void);
void slub_shutdown(void);
void *slub_alloc(size_t size);
int slub_free(void *object);
size_t slub_nr_slabs(void);
size_t slub_nr_live_objects(void);
int slub_check(void);
```

SLUB 使用物理页分配器提供的页面组织对象。小对象采用 8、16、32、64、128、256、512、1024、2048 字节九个缓存，按请求大小向上选择最近的缓存。每个 slab 通过 `alloc_pages(1)` 取得一页，页首保存管理信息，其余空间组织固定大小槽位；对象头记录所属 slab、状态及空闲链信息。

超过 2048 字节的对象通过独立连续页路径分配，页头记录释放所需信息，并与用户可写范围隔离。空 slab 立即归还底层页面；`slub_shutdown()` 回收仍持有的缓存页和大对象页，清空统计并退出初始化状态。

分配失败返回 `NULL`，释放状态使用 `SLUB_OK`、`SLUB_ERR_INVALID`、`SLUB_ERR_DOUBLE_FREE` 和 `SLUB_ERR_UNINITIALIZED`。`slub_free(NULL)` 为空操作。实现面向单核串行环境，不保证安全识别任意无效指针。

#### 最终提示词

我们将对象分配器与独立测试分别描述，使测试从公开行为检验实现，而不是重复分配器内部逻辑。

<a id="slub-implementation-prompt"></a>

##### 对象分配器实现提示词

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

<a id="slub-test-prompt"></a>

##### 独立测试提示词

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

#### 实现与问题修正

我们先实现固定缓存、连续页大对象路径和自检接口。编译通过后，完整对象读写暴露出大对象用户区域与释放元数据重叠的问题：写入对象内容会破坏链表信息，使后续回收不再可靠。我们将公共对象头与用户区域分离，并使用独立的大对象记录重新验证。

随后，固定数量的记录又限制了同时存活的大对象数量。我们改为由每个大对象的页头保存链表节点，去掉这一固定记录上限。默认内核通过学生新增的组合回调，在原 Best-Fit 检查后运行 SLUB 自检，原测试和启动流程保持不变。

最终，三种页分配器、四种 RAM 配置的 12 组组合检查均通过。相比只看对象是否能分配成功，我们更关注写入完整请求范围后能否正确释放，以及 slab 全空后页面是否真正归还底层分配器。

#### 测试覆盖与有效性

测试覆盖零尺寸分配和空指针释放，以及 1、7、8、15、24、63、64、127、128、255、256、511、512、1024、2048、2049 字节请求。每个对象写入并读回完整请求范围，同时检查存活对象互不重叠，用于发现槽位选择、边界计算和元数据覆盖错误。

部分释放与再次分配检查对象复用；重复释放检查错误码及其他存活对象是否受影响；跨页大对象检查连续页路径。最后检查空 slab 回收，以及存在活跃小对象、大对象时的 shutdown 和重新初始化。自检结束时存活对象数和 slab 数均为零，`nr_free_pages()` 恢复初值，从而检验底层页面是否泄漏。

First-Fit、Best-Fit、Buddy 与 64/70/128/256 MiB RAM 的 12 组组合检查均通过。实现未覆盖 NUMA、per-CPU 缓存、并发锁及完整 Linux SLUB 的其他机制。

### 4.6 分支任务：GDB 调试页表查询与 TLB

**负责人：** 2412439-崔迪。

#### 调试目的与观察方法

我们希望从一条真实访存指令出发，说明虚拟地址如何经过 TLB 查询和页表解析变成物理地址。本节记录 2026-10-25 在临时 First-Fit 内核上的调试，以下符号和页表地址均对应这次构建。

<a id="qemu-source-setup"></a>

我们使用 QEMU 4.1.1 的源码及带调试符号、未剥离的原生构建。宿主机 GDB 调试 QEMU 进程，观察模拟器如何处理访存；guest GDB 连接 QEMU 的远程调试接口，观察内核指令与寄存器。这两个层次的地址和执行对象不同，不能把宿主机指针当作 guest 物理地址。环境准备命令保存在 [仓库说明](../README.md#qemu-源码调试环境)。

实际观察采用指导书允许的条件断点思路：选取确定会访问的 DTB 地址，限制翻译函数中的访问地址和访问类型。QEMU 源码跟踪与 guest 指令核对分别运行相同内核配置，避免把 GDB 自己读取内存产生的查询误认为 CPU 指令访存。保存的 [调试日志](logs/20261025-mmu.txt) 和第五章图 5 给出了断点、调用栈及数值。

#### TLB 查询位置与关键调用路径

在 QEMU 4.1.1 中，我们将关注点从 `target/riscv/cpu_helper.c` 扩展到 `accel/tcg/cputlb.c`。前者包含 RISC-V 的页表翻译逻辑，后者包含软件 TLB 查询及慢路径处理。真实 DTB 加载所经过的慢路径为：

```text
TCG 生成的访存代码
  → full_le_ldul_mmu / load_helper
  → tlb_fill
  → riscv_cpu_tlb_fill
  → get_physical_address
  → tlb_set_page / tlb_set_page_with_attrs
```

`load_helper()` 通过 `tlb_index()`、`tlb_entry()` 取出条目并检查标签；主条目和 victim TLB 均未命中时才进入填充路径。本次读取前的读标签为 `0xffffffffc0200000`，目标页为 `0xffffffffc2200000`，二者不同。结合后续调用栈，我们确认这次访问进入了未命中的慢路径；这不意味着每次访存都会遍历页表。

源码中的关键判断及本次观察如下：

| 判断或分支 | 含义 | 本次观察 |
|------------|------|----------|
| 软件 TLB 标签匹配 | 决定能否使用已缓存的映射 | 目标页与旧读标签不匹配，进入填充路径 |
| SATP.MODE 与特权状态 | 决定直通还是分页翻译 | DTB 数据读取使用 Sv39；另一次裸地址取指的 SATP 为零 |
| `!(pte & PTE_V)` | 检查页表项是否有效 | PTE 为 `0x200000cf`，V 位有效 |
| `!(pte & (PTE_R \| PTE_W \| PTE_X))` | 无访问权限位的有效项表示下一级表；具有权限位时可进入叶子处理 | 当前项含 R/W/X，是根级叶子 |
| 叶子权限、大页对齐及 A/D 位 | 检查该访问是否可完成并维护访问状态 | 翻译成功，返回 `prot=7`；该 PTE 已含 A/D 位 |

#### 单步观察一次 Sv39 地址翻译

guest GDB 在 `dtb_init()` 中停于 `PC=0xffffffffc020028a`，指令为 `lw a5,0(a4)`，`a4=0xffffffffc2200000`。我们跟踪的是读取 DTB 头部的实际加载，而不是为了演示而新增的访存代码。

| 检查项 | 实测值 |
|--------|--------|
| SATP | `0x8000000000080205` |
| 根页表物理地址 | `0x80205000` |
| Sv39 层数 / 当前循环 | `levels=3` / `i=0` |
| VPN[2] / ptshift | `511` / `18` |
| PTE 物理地址 / 内容 | `0x80205ff8` / `0x200000cf` |
| 翻译结果 / 权限 | `PA=0x82200000` / `prot=7` |
| 填充后的 TLB 读标签 | `0xffffffffc2200000` |

SATP 的页号字段给出根表物理地址，VPN[2]=511 定位到 `0x80205ff8`。单步后我们看到该 PTE 直接进入叶子处理，而不是继续访问下级表。原因是其 R/W/X 位已设置，表示 1 GiB 映射；Sv39 有三层地址索引，并不要求每次翻译都读取三张表。

QEMU 将叶子中的物理页号与低位 VPN、页内偏移组合，得到 `0x82200000`，随后为当前访问填充软件 TLB。使用该条目的 host RAM 加数读取到 `d0 0d fe ed`。guest 指令单步后的低 32 位为 `0xedfe0dd0`，正是这些 DTB 魔数字节的小端加载结果，说明两侧观察的数据一致。

#### 裸地址模式对比与对软件 TLB 的理解

在 SATP=0 时，我们还跟踪了 `0x80200000` 处的取指。地址翻译结果为 `VA=PA=0x80200000`，但 QEMU 仍调用 `tlb_set_page_with_attrs()`。这说明 QEMU 软件 TLB 不只服务页表翻译，还保存 guest 地址到 host RAM 访问位置及属性的关系。

本次页表叶子覆盖 1 GiB，而软件 TLB 按 4 KiB 粒度填充访问条目，两者不矛盾。我们不能把模拟器中的缓存组织直接当作真实 CPU 的微架构 TLB，也不能因为软件实现中出现函数调用，就认为真实处理器需要内核执行这些函数。

#### 调试中借助 AI 解决的问题

我们使用 AI 帮助定位源码、解释断点现象和分析寄存器值，再用源码及实际运行核对。下面按问题和验证过程概括交互内容：

| 遇到的问题与最初理解 | AI 辅助分析的方向 | 我们的核对结果 |
|----------------------|------------------|----------------|
| 运行内核的 QEMU 可以正常启动，但普通安装版不便设置源码断点 | 区分系统安装版和带符号的源码构建，并区分 host、guest 两种 GDB 的目标 | 使用未剥离构建后，能够停在 QEMU 源码及内核指令处 |
| 只看翻译函数仍不能说明 TLB 是否先被查询 | 查找 `cputlb.c` 的标签检查和慢路径，与 RISC-V 翻译函数建立联系 | 旧读标签不匹配，调用栈随后进入 `get_physical_address()` |
| Sv39 为什么在第一个页表项就结束，没有看到完整三层循环 | 根据有效位和权限位区分下级表指针与大页叶子 | `0x200000cf` 为根级叶子，`i=0` 即可完成翻译 |
| GDB 显示的 SATP 可能为负数，字段判断与预期不符 | 按 64 位无符号位模式解释寄存器 | 规范后得到 `0x8000000000080205`，根表地址与实际 PTE 位置一致 |

此外，日志中出现 GDB 无法关闭地址随机化的权限告警，但后续断点与数据检查成功。我们据此将其识别为环境提示，而没有通过修改内核来规避告警。最有启发的现象是“裸地址模式也填充软件 TLB”和“Sv39 在根级叶子提前结束”；这两点使我们不再把示意图中的固定步骤直接等同于实际执行过程。

### 4.7 选做思考题：未知硬件的可用物理内存范围获取

**负责人：** 2412439-崔迪。

> 如果 OS 无法提前知道当前硬件的可用物理内存范围，有何办法获取？

我们的方案是由固件或引导程序提供硬件内存描述，内核在启动时解析，而不是在代码中预先写死全部 RAM 容量。内核除了需要知道 RAM 的位置，还需要知道哪些区间已被占用，因此“获得 RAM 范围”和“确定可分配页面”是两个步骤。

本实验采用 DTB：QEMU 的 `hw/riscv/virt.c:create_fdt()` 将 RAM 基址与容量写入 memory 节点，启动参数把 DTB 地址传给内核，`dtb_init()` 解析后由 `get_memory_base()`、`get_memory_size()` 提供结果。`page_init()` 再结合框架上限、内核末端、描述符占用和页面对齐计算空闲区。这条数据流使同一内核能够读取不同 RAM 配置，而不需要为每种容量重新填写常量。

取得硬件描述后仍不能把整个区间登记为空闲页。固件、内核镜像、页描述符及其他保留区域必须排除，设备地址也不能当作普通 RAM 使用。以本实验为例，256 MiB 配置能够被 DTB 描述，但现有框架仍只管理到 `0x88000000`；管理范围内还要扣除已有占用。

对于更完整的平台，描述可能包含多段 RAM 和单独的保留区，内核需要逐段校验、排除占用并登记完整页面。本实验的 getter 只提供单段 RAM 信息，因此这里只给出扩展思路，不声称已经实现多段内存管理。这个问题的关键是建立启动环境与内核之间的内存描述约定，而不是依靠试写未知地址来猜测可用内存。

## 五、测试与验证

### 5.1 验证范围与结果
结果如下。

| 检查项目 | 结果 |
|----------|------|
| First-Fit、Best-Fit、Buddy × 64/70/128/256 MiB，与 SLUB 组合运行 | 12 组全部通过；Buddy 每组完成两轮自检 |
| `page_init()` 边界用例 | 12/12 通过 |
| 原课程 `make grade` | page init、best fit 均 OK，**20/20** |

完整输出见 [测试日志](logs/2412819-tests.txt)。

### 5.2 构建与复现命令

在 Linux/WSL 或课程容器的仓库 `code/` 中执行：

```bash
make clean
make
make qemu
# 自检成功后，用 Ctrl+A、再 X 退出 QEMU，随后评分
make grade
```

独立验证命令如下，其中管理器切换及额外检查只作用于临时副本，不写回课程框架：

```bash
python3 tools/lab2_verify.py --ram 64 70 128 256
python3 tools/lab2_verify.py --backend buddy --ram 128
python3 tools/check_page_init.py
bash tools/verify_init.sh
python3 tools/debug_mmu.py --qemu-source /tmp/qemu
```

macOS 宿主机可通过 `docker exec os-labs-lab-1 ...` 执行容器命令，交互启动使用 `docker exec -it os-labs-lab-1 make qemu`。容器名沿用既有环境，挂载内容为当前 Lab 2 的 `code/`。原 `Makefile` 不提供 `PMM` 切换参数；完整复现使用上述验证工具。

内核完成自检后进入永久循环，验证工具在确认成功标记后有界停止，因此不能仅凭外部超时判断测试失败。

### 5.3 实验结果截图

五张截图对应构建、默认启动、课程评分、Buddy 与 SLUB 组合验证以及 MMU/TLB 调试。图片保留原始内容与分辨率；第五张为完整长图，可通过原图链接查看细节。

#### 图 1：清理、编译与构建产物

![清理后重新编译成功，并列出 kernel 与 ucore.img](images/2412439-final-build.png)

编译、链接和镜像导出均成功。`bin/kernel` 为包含符号等信息的 ELF 文件，大小为 129K；`bin/ucore.img` 由 `objcopy --strip-all -O binary` 导出，大小为 29K。两种产物格式不同，大小差异符合预期。

#### 图 2：默认 Best-Fit 与 SLUB 启动自检

![默认 Best-Fit 管理器启动，SLUB 和页面分配自检通过](images/2412439-final-boot.png)

管理器为 `best_fit_pmm_manager`，`slub_check()` 和 `check_alloc_page()` 均输出 `succeeded!`。RAM 为 128 MiB，范围为 `0x80000000` 至 `0x87ffffff`，管理结束边界为 `0x88000000`。根页表虚拟地址 `0xffffffffc0206000` 对应物理地址 `0x80206000`，与直接映射关系一致。

#### 图 3：课程评分

![课程评分的 page init 和 best fit 均为 OK，总分 20/20](images/2412439-final-grade.png)

page init、best fit 均为 OK，总分为 **20/20**。`lab2 injection complete` 与 `/tmp/lab2_grade` 为课程脚本注入测试及临时构建的正常输出；编译信息合并显示不影响评分结果。

#### 图 4：Buddy 与 SLUB 组合验证

![Buddy 连续两轮检查以及 SLUB 自检通过](images/2412439-buddy-slub.png)

管理器为 `Buddy System`，两轮初始空闲页数均为 31919，页面分配和 SLUB 检查均通过。计数与地址范围一致：`(0x88000000 - 0x80351000) / 4096 = 31919`。

该临时内核的内存占用为 65KB，默认内核为 29KB。管理器、检查代码和数据不同，使空闲起点和页表位置相应变化，二者属于不同配置。

#### 图 5：MMU/TLB 源码与指令调试

![完整 MMU 和 TLB 调试截图，包含断点、单步及最终 PASS](images/2412439-mmu-full.png)

[查看完整原图（2932 × 5818）](images/2412439-mmu-full.png)

裸地址模式下 `VA=PA=0x80200000`；Sv39 下 `0xffffffffc2200000` 翻译为 `0x82200000`。页表叶子、TLB 填充和 DTB 数据与第四章分析一致，最后输出 `PASS: QEMU source MMU/TLB and guest instruction traces completed.`。

GDB 的 `Error disabling address space randomization: Operation not permitted` 表示容器环境下未能关闭地址随机化。后续断点、单步及数据检查均成功，该告警未阻断验证。`<optimized out>` 为变量优化信息，线程切换与末尾的 `detached` 为正常调试过程输出。

## 六、实验总结与收获

### 对操作系统的理解

#### 实验知识点与操作系统原理

| 实验内容 | 对应原理及联系 | 实现中的区别与边界 |
|----------|----------------|--------------------|
| DTB 与 `page_init()` | 硬件资源发现和启动内存管理 | RAM 描述、框架管理上限、实际可分配页数不同 |
| `Page` 与保留区 | 页框状态、元数据及资源所有权 | 描述符不是物理页本身，`pages[i]` 对应 `nbase+i` 页号；内核及描述符占用不进入空闲区 |
| First-Fit / Best-Fit | 连续空间放置策略与外部碎片 | 选择策略不改变地址翻译，最小适配也不保证全局碎片最少 |
| Buddy | 分级管理、拆分及伙伴合并 | 同阶 XOR 伙伴才能合并，请求向上取整产生内部碎片 |
| SLUB | 页内对象缓存和两层内存管理 | 对象复用与页面回收分属不同层次，缓存为空后才能归还底层页 |
| 页表与 TLB | 虚实地址映射、权限和翻译缓存 | Sv39 可在上层叶子终止遍历；QEMU 软件 TLB 还承担 host RAM 访问映射 |
| 回收自检 | 资源守恒与接口契约 | 对象释放、slab 回收和空闲页数恢复需要分别检查 |

#### 重要但本实验尚未涉及的 OS 原理知识点

1. **进程地址空间与隔离。** 操作系统通常为不同进程建立独立地址空间，通过页表和访问权限隔离资源。本实验使用启动阶段的内核映射，未实现用户进程、进程页表切换或跨进程内存保护。
2. **按需分页与缺页异常处理。** 按需分页允许先建立虚拟区域，在首次访问缺失页面时通过异常分配物理页并更新映射。本实验直接管理已有物理页，未实现这一缺页处理流程；连续页分配失败也不等同于发生缺页异常。
3. **页面置换与交换。** 物理内存不足时，操作系统可以选择驻留页换出到外存，并在需要时换入。本实验的 First-Fit、Best-Fit 和 Buddy 只选择空闲空间，不选择换出页面，也没有实现置换算法或交换区管理。
4. **多核同步与 TLB 一致性。** 多核同时分配或释放内存时，需要同步保护共享管理结构；页表映射变化还可能需要通知其他核使旧 TLB 项失效。本实验在单核串行条件下验证，未实现并发分配的锁机制或跨核 TLB 失效通知。
5. **进程调度与资源回收。** 完整操作系统需要根据调度策略切换进程，并在进程退出时回收其资源。本实验尚未建立进程生命周期，页面和对象由自检代码主动归还，不能等同于进程级资源管理。

### AI 协作开发的经验

这次实验中，我们先核对真实头文件，再把行为要求写成前后置条件。页号上界与描述符数量、Buddy 的伙伴关系以及 SLUB 的元数据边界都不能由函数名推断；把这些关系写清楚，才能判断生成代码是否符合内核的资源约定。

实现与测试分开描述后，我们能够从公开接口检查容量、地址、内容和回收结果。Buddy 测试记录上限的问题说明，测试程序也可能误用范围常量；SLUB 的元数据覆盖问题则说明，编译通过并不代表对象能够安全写入和释放。我们通过实际读写、保留状态检查与资源计数恢复来定位问题，而不是调整成功输出。

GDB 调试中，AI 帮助我们缩小源码范围和解释现象，结论仍由断点、调用栈、PTE 和指令数据核实。我们最终理解了上层页表叶子为什么能提前结束遍历，也看到 QEMU 软件 TLB 在裸地址模式下仍有用途；这些观察比复述固定的地址翻译步骤更能说明实际运行过程。

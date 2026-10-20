# 操作系统实验仓库：Lab 2

每次实验使用独立分支。当前 `lab2` 从 `lab1` 创建，`code/` 已更新为课程提供的 `lab2_new` 初始源码；Lab 1 的提交、报告和截图保存在 `lab1` 分支。

## 当前状态

- 已导入 Lab 2 的 39 个课程源码与工具文件，包括 DTB、启动分页、物理内存管理框架和评分脚本。
- 崔迪已完成 `page_init()`、独立验证配置和 GDB 页表/TLB 调试；Best-Fit、Buddy、SLUB 仍由对应成员实现。课程测试和其他成员的实现文件保持原样。
- 显式 First-Fit 配置已通过构建、12 个初始化边界用例、64/128/256 MiB 启动自检、GDB 页面状态和 QEMU 源码调试。最终 Best-Fit 集成、课程评分及 Buddy/SLUB 验收等待对应实现完成。
- 保留本机已有的 `code/dev` 与 `code/environment/`，它们是本地辅助文件，不属于课程源码，也不在当前仓库跟踪范围内。原 Lab 1 的构建产物和截图已从 Lab 2 交付目录清理。

## 目录

```text
code/                         Lab 2 课程源码，在此目录构建
report/report.md              基于课程模板的 Lab 2 报告，已填写人员和计划分工
report/实验报告模板.md         课程原始报告模板，保持原样
report/prompt.md              最终提示词汇总
report/members/               各成员独立维护的报告与提示词草稿
report/images/                Lab 2 真实测试截图，文件名使用学号前缀
report/logs/                  实际初始化与 MMU/TLB 调试输出
report/changes/               按成员记录修改文件范围
```

## 三人分工与文件边界

| 成员 | 任务 | 唯一主责代码文件 |
|------|------|------------------|
| 2412439-崔迪 | 初始化、公共接入、GDB 页表/TLB 调试及最终汇总 | `code/kern/mm/pmm.c`、`code/kern/init/init.c`、`code/Makefile`；启动、DTB、公共头文件与配置如需修改统一接入 |
| 2412819-李子谦 | First-Fit 分析、Best-Fit、Buddy 及测试 | `code/kern/mm/best_fit_pmm.c`、`best_fit_pmm.h`；新增 `buddy_pmm.c`、`buddy_pmm.h` 和必要的 `buddy_check.c` |
| 2412852-梁靖泽 | SLUB 及测试、总结初稿 | 新增 `code/kern/mm/slub.c`、`slub.h`、`slub_check.c` |

新增分配器和测试文件均放在 `code/kern/mm/`。完整分工、接口约定及报告章节分配见 [报告的小组分工](report/report.md#小组分工)。

- 公共文件只由崔迪修改；其他成员通过自己的头文件声明接口，并提供接入说明。
- Best-Fit 与 Buddy 遵循 `struct pmm_manager`，SLUB 调用 `alloc_pages()` / `free_pages()`。
- 最终默认验收切换为 Best-Fit；Buddy 使用独立配置验收，切换与启动接入由崔迪统一处理。
- 保持 Best-Fit 的 `basic_check()`、`best_fit_check()` 和课程评分文件不变；本人新增实现中的 `YOUR CODE` 按要求替换为实际作者学号。
- 每人修改自己的 `report/members/学号-姓名.md`，记录 spec、原始提示词、迭代和测试。崔迪最终整合 `report.md`、`prompt.md`，避免三人同时修改这两个汇总文件。
- 三人可并行编写各自模块；崔迪优先提交初始化公共基线，其他成员同步后进行内核运行测试。模块私有状态使用文件内 `static`，新增外部符号使用明确前缀以减少链接冲突。

## 开发与验证

当前可独立验证初始化。在已安装 RISC-V 工具链、QEMU、GDB、Python 3 及宿主机 C 编译器和头文件的 Linux/WSL 环境中：

```bash
cd code
make PMM=first_fit
make qemu PMM=first_fit
# 退出交互 QEMU 后运行自动验证：
bash tools/verify_init.sh
```

`PMM=first_fit` 显式排除尚未完成的 Best-Fit 模板，使用课程现有 First-Fit 检查初始化。默认 `PMM=best_fit` 为最终课程配置，等待李子谦完成实现后再运行 `make`、`make qemu` 和 `make grade`。构建配置切换时自动重新编译对象文件。

在 macOS 上可直接运行初始化的宿主机边界测试：

```bash
python3 code/tools/check_page_init.py
```

本机原有容器已补充 `python3`、`libc6-dev`、`bc`，可运行：

```bash
docker start os-labs-lab-1
docker exec os-labs-lab-1 bash tools/verify_init.sh
docker exec os-labs-lab-1 python3 tools/debug_mmu.py --qemu-source /tmp/qemu
```

`debug_mmu.py` 需要 QEMU 4.1.1 源码及未剥离符号的构建文件；本次已准备在容器 `/tmp/qemu`。新环境的准备方法见 [崔迪个人记录](report/members/2412439-崔迪.md#qemu-源码准备与复现)。原有本机 Docker 验证脚本针对 Lab 1，本轮使用上述 Lab 2 新工具。

实测日志见 [初始化验证](report/logs/2412439-initialization.txt)、[MMU/TLB 调试](report/logs/2412439-mmu.txt)。文件变更边界见 [2412439 改动记录](report/changes/2412439-initialization.md)。

课程 `make grade` 在临时副本中注入官方测试；本轮未修改这些测试，也未宣称 Best-Fit 课程评分通过。Buddy、SLUB 仍需对应成员实现和自编测试。

## 协作与提交

三人各自使用独立 clone，在 `lab2` 上工作。开始前同步远程，仅提交本人负责文件，推送前同步最新提交。若要调整跨成员接口，先确定约定，再由公共文件负责人接入。

代码、报告、提示词和真实截图放在同一个 `lab2` 分支中提交。完成 Lab 2 后再从其完成状态创建 `lab3`，导入 Lab 3 框架并迁移本人完成的 Lab 2 实现。

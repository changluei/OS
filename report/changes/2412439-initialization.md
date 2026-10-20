# 2412439-崔迪：本轮文件改动范围

日期：2026-10-20（Asia/Shanghai）。工作分支为 `lab2`，开始时 HEAD 为 `5a6a3cf`。开始前主报告已有 DAG 未提交改动，本轮保留并补充其状态及独立验证路径。

## 实际修改与新增文件

| 文件（相对仓库根目录） | 类型 | 本轮内容 |
|----------------------|------|----------|
| `code/kern/mm/pmm.c` | 修改 | 实现 `page_init()`；保留 `lab2` 标记并署名 2412439；按构建宏选择 First-Fit/Best-Fit |
| `code/Makefile` | 修改 | 默认最终 Best-Fit 配置；显式 First-Fit 独立验证；过滤未完成 Best-Fit 源文件；切换配置时重编译 |
| `code/tools/check_page_init.py` | 新增 | 编译真实初始化函数体的 12 个边界用例，替换链接器符号并模拟外部依赖 |
| `code/tools/verify_init.sh` | 新增 | Linux 实测：构建、对象配置切换、三种 RAM 启动、GDB 页状态和 Sv39 检查 |
| `code/tools/debug_mmu.py` | 新增 | QEMU 源码条件断点、TLB 未命中/填充、裸地址对比及对应 RISC-V 读取指令 |
| `README.md` | 修改 | 当前完成状态、复现命令、依赖和文件边界入口 |
| `report/report.md` | 修改 | 初始化实现、环境、逻辑主线、DAG 状态、GDB 调试、Challenge 方案和实际验证结果 |
| `report/members/2412439-崔迪.md` | 修改 | 本人的 spec、原始请求、排查、测试、源码调试和复现记录 |
| `report/prompt.md` | 修改 | 追加本轮用户原始请求与实现记录索引 |
| `report/logs/2412439-initialization.txt` | 新增 | 实际初始化与 GDB 检查日志 |
| `report/logs/2412439-mmu.txt` | 新增 | 实际 QEMU 源码和内核访存调试日志 |
| `report/changes/2412439-initialization.md` | 新增 | 本文件，记录本轮改动范围与协作交接 |

核心生产代码仅修改 `pmm.c` 和 `Makefile`；另新增三份本人验证工具。报告中的其他分配器章节保留待负责人完成状态。

## 本轮保持原样的协作范围

- 李子谦负责的 `best_fit_pmm.c`、`best_fit_pmm.h`、个人草稿，以及待新增的 Buddy 模块。
- 梁靖泽的个人草稿及待新增的 SLUB 模块。
- 课程 First-Fit 实现、入口汇编、`init.c`、DTB 解析与公共头文件。
- 课程评分文件 `tools/test_lab2.c`、`test_lab2.h`、`inject.py`、`grade.sh`；Best-Fit 两个原有测试函数未改动。
- `lab2_new` 原始下载目录、原始报告模板、`lab1` 分支及其历史提交。

## 环境操作（非仓库源码改动）

启动本机 Docker Desktop 和已有的 `os-labs-lab-1` 容器；在该容器中安装 `python3`、`libc6-dev`、`bc`。构建 `os-labs-qemu-builder:4.1.1` 并把其中 `/tmp/qemu` 的源码和未剥离二进制复制到 OS 容器 `/tmp/qemu`。没有替换系统安装的 QEMU，也未修改被忽略的 `code/dev`、`code/environment/` 文件。

`code/obj/`、`code/bin/` 为验证产生的已忽略构建产物；临时 GDB 命令文件位于容器临时目录，验证完成后清理。源码与构建环境保留供后续调试复现。

## 已完成验证

- `page_init()` 宿主机边界用例：12/12。
- 真实 RISC-V 构建、First-Fit/Best-Fit 配置对象切换检查。
- QEMU 4.1.1：64/128/256 MiB 的启动、初始化及已有 First-Fit 分配自检。
- GDB：保留页、空闲页数、SATP、页表基址及根级大页映射。
- QEMU 源码：实际数据读取的调用链、PTE 解析、TLB 标签未命中和填充；与内核 `lw` 指令读取的 DTB 字节核对；裸地址模式对比。

日志链接：[初始化验证](../logs/2412439-initialization.txt)、[MMU/TLB 调试](../logs/2412439-mmu.txt)。

## 后续交接

李子谦完成 Best-Fit 并提交后，崔迪使用默认构建完成最终基线及课程 `make grade`。Buddy、SLUB 的实现和自编测试由对应负责人交付，公共接入由崔迪统一处理。默认 Best-Fit 配置、Buddy、SLUB 的最终集成结果仍待验证，本轮未宣称其通过。

本轮未进行 Git 提交或推送。

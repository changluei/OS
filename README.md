# 操作系统实验仓库：Lab 2

当前分支为 `lab2`，代码基于课程 `lab2_new`。Lab 1 的代码与报告保留在 `lab1` 分支。

## 完成情况

初始化、Best-Fit、Buddy、SLUB、SLUB 默认接入和页表/TLB 调试已完成。2026-10-25 复验结果：课程评分 20/20，三个页分配器与四种 RAM 的 12 组 SLUB 组合检查通过，初始化边界用例 12/12。五张真实终端截图已补齐，逐张核对结果收入主报告第五章，并保留实际文本日志。

原有 39 个课程文件中，37 个与下载版逐字节一致；`pmm.c` 和 `best_fit_pmm.c` 补充实验任务代码及指定接入。之前为验证增加的 `Makefile` 选项已撤回，构建文件、启动流程、公共头文件、链接脚本、评分脚本和原测试函数保持原样。

## 目录与分工

```text
code/                         课程源码、实验功能与独立验证工具
report/report.md              正式实验报告，含分工、实现分析与测试
report/prompt.md              按 Lab 0.5 四段式编写的任务提示词
report/logs/                  当前测试、初始化与 MMU 调试日志
report/images/                五张真实终端截图及核对索引
```

| 成员 | 实际任务 |
|------|----------|
| 2412439-崔迪 | 初始化、公共检查接入、页表/TLB 调试、框架审计与报告汇总 |
| 2412819-李子谦 | First-Fit 分析、Best-Fit、Buddy 与独立页级测试 |
| 2412852-梁靖泽 | SLUB 对象分配、自编测试与对象分配总结 |

后续修改继续遵守实验指定位置和新增功能文件的边界；不为切换测试后端改动课程框架。

## 构建、运行和评分

在安装课程 RISC-V 工具链、QEMU 4.1.1 和 GDB 的 Linux/WSL 环境中：

```bash
cd code
make clean
make
make qemu
# 显示自检通过后，Ctrl+A 再 X 退出 QEMU：
make grade
```

默认选择 Best-Fit。学生检查回调先运行课程原 Best-Fit 测试，再运行 `slub_check()`；无需改 `init.c`。原 `Makefile` 没有 `PMM` 切换接口，旧分支记录中的 `make PMM=...` 不再是当前复现命令。

## 独立验证

以下新工具只在临时副本中切换管理器和接入额外检查，完成后清理副本：

```bash
cd code
python3 tools/lab2_verify.py --ram 64 70 128 256
python3 tools/lab2_verify.py --backend buddy --ram 128
bash tools/verify_init.sh
python3 tools/debug_mmu.py --qemu-source /tmp/qemu
```

最后一个命令需要 QEMU 4.1.1 源码及未剥离的构建文件。本机容器已保留 `/tmp/qemu`；新环境准备见 [下方源码调试环境](#qemu-源码调试环境)。初始化宿主机用例也可在 macOS 上执行 `python3 code/tools/check_page_init.py`，需要本机 C 编译器。

本机原有容器名为 `os-labs-lab-1`，名称与当前实验无关。它挂载当前 `code/`，可使用 `docker exec os-labs-lab-1 ...` 执行以上命令。原本地 `code/dev`、`code/environment/` 保持不变且未纳入 Git；其中旧验证流程面向 Lab 1，不作为 Lab 2 的复验入口。

### QEMU 源码调试环境

源码调试需要 QEMU 4.1.1 源码及带调试符号、未剥离的构建文件。本次使用容器 `/tmp/qemu` 下已有构建；以下命令供新 Linux/WSL 环境准备相同版本使用，不替换系统安装的 QEMU。

```bash
sudo apt-get update
sudo apt-get install -y gcc g++ make pkg-config python3 wget xz-utils bzip2 \
  libglib2.0-dev libpixman-1-dev libfdt-dev gdb-multiarch
wget https://download.qemu.org/qemu-4.1.1.tar.xz
tar -xJf qemu-4.1.1.tar.xz
cd qemu-4.1.1
./configure --python=python3 --target-list=riscv64-softmmu \
  --enable-debug --disable-strip --disable-werror --disable-docs \
  --disable-tools --disable-guest-agent --disable-capstone
make -j4
```

准备后，在仓库 `code/` 中运行 `python3 tools/debug_mmu.py --qemu-source /path/to/qemu-4.1.1`，将路径替换为实际构建目录。实验观察及结论见 [报告 GDB 部分](report/report.md#qemu-source-setup)。

## 交付证据

- [当前版本构建、12 组组合检查与课程评分 20/20](report/logs/2412819-tests.txt)
- [初始化与页表状态](report/logs/20261025-initialization.txt)
- [QEMU 源码与 RISC-V 指令调试](report/logs/20261025-mmu.txt)
- [真实截图与核对结果](report/images/README.md)

报告统一引用 2026-10-25 的复验日志，环境来源和测试覆盖范围在主报告中说明。

## 报告与提交

实验内容统一记录在 `report/report.md`，规范化提示词保存在 `report/prompt.md`，相关日志和截图分别存放在 `report/logs/`、`report/images/`。三份个人记录已整合到主报告后删除。提交时包含代码、报告及引用附件；课程测试和框架文件保持原样。

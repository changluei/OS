# 操作系统实验仓库：Lab 3 中断与异常

当前分支为 `lab3`，以同级 `../lab3_new/` 的课程代码为基线，已完成 Lab 2 前置代码迁入、Trap 上下文恢复与入口、中断与异常处理。课程评分 100/100，正常运行输出 10 行 `100 ticks` 后通过 SBI 自动关机。2026-11-08 在现有 Docker 环境中复验，结果与 2026-11-04 的 WSL 验收一致。

代码、四段式提示词、思考题、报告正文和三张 Lab 3 真实截图均已整理。构建与评分截图完整；启动截图底部截断，自动关机由同日完整复验日志补充证明，报告已注明。Lab 2 正文在 `report/archive/`，原有图片仍作归档。当前仅余本轮材料的 Git 提交与推送。

## 实验范围与依据

本地指导书位于 `../lab2026/_book/`，重点包括 Lab 3 的练习、Trap 入口、中断处理、时钟、中断保护及报告要求，以及 Lab 0.5 的提示词结构与实践指南。

按用户确认，Challenge 1 是误放内容，本实验不实现用户态切换。栈回溯空函数、未使用 SBI 声明和旧平台工具也不属于当前任务，保留原样。

实验主线为：`stvec` 指向汇编入口 → `SAVE_ALL` 保存现场 → `__alltraps` 将当前 frame 传给 `trap(tf)` → C 层分发并处理事件 → `RESTORE_ALL` 恢复现场 → `sret` 返回。本实验主要验证 S 态内核中的 Trap 往返。

## 实际分工

| 成员 | 实现 | 验证与文档 |
|------|------|------------|
| 2412439-崔迪 | 模块一：`RESTORE_ALL`、`__alltraps`；两处 `lab3 function1` 署名 `2412439` | 现场恢复验证、模块一四段式提示词、当前环境复验与截图采集 |
| 2412819-李子谦 | `interrupt_handler()`：中断分类、连续时钟、计数输出、关机；署名 `2412819` | 课程评分、中断/关机边界及 SIE 检查、中断提示词与临界区思考题 |
| 2412852-梁靖泽 | `exception_handler()` 及指令判长：断点、非法指令、EPC 更新与兜底；署名 `2412852` | 异常补充验证、`stvec` 思考题、模块一说明整理、原理总结与报告整合 |

三人的原始代码贡献和资料整理分别记录。`report/prompt.md` 按成员保存提示词；崔迪部分于 2026-11-08 按课程四段式补充整理，队友原有提示词正文保留，不将整理版冒充历史对话的逐字转录。

## 代码边界

- Lab 3 修改集中在 `trapentry.S` 的两个 `lab3 function1` 区域和 `trap.c` 的 `lab3 function2` 区域，保留标记并署实际负责人学号。SBI 头文件依赖也位于填空区域内。
- `SAVE_ALL`、`struct trapframe`、既有初始化/打印/分发函数、Makefile、链接脚本和课程评分工具保持原样。
- `pmm.c`、`best_fit_pmm.c` 的前置实现从 Lab 2 迁入，保留原署名；默认内存管理器保持课程的 First-Fit。
- 自编异常独立测试脚本不是课程要求，现已删除。已有 `2412852-exceptions-*.txt` 日志和队友提示词中的测试设计仅作为历史记录，不再作为当前运行入口，也不要求其独立截图。

## 完成状态

- [x] 创建 `lab3` 分支，切换课程基线并迁入 Lab 2 前置实现。
- [x] 完成 `RESTORE_ALL` 和 `__alltraps`，保留 `SAVE_ALL` 和框架接口。
- [x] 完成中断分类、定时器重设、计数、统计输出与真实 SBI 关机。
- [x] 完成 16/32 位断点、非法指令的判长和 EPC 更新，以及未知异常兜底。
- [x] 保留 `lab3` 标记并分别署三人的学号，恢复填空区域外的框架内容。
- [x] 编译、正常运行、关机和课程评分通过；三项分别为 35/35、25/25、40/40。
- [x] 完成 `stvec` 兼容性、`do { } while (0)`、SIE 保存/恢复及原理总结。
- [x] 补齐三人的报告章节、工具/环境来源及模块一四段式提示词。
- [x] 删除非课程要求的异常独立测试脚本，清理失效的复现说明。
- [x] 三张真实截图已收入 `report/images/` 并插入报告；已核对构建与总分，启动图的截断范围及关机日志依据分别说明。
- [x] 最终核对图片引用、四段式提示词、日志结果、课程测试及框架修改边界。
- [ ] 提交并推送本轮整理。

## 构建与验收

当前 macOS 使用容器 `os-labs-lab-1`，挂载仓库 `code/` 到 `/workspace`。容器提供 RISC-V GCC 10.2.0、QEMU 4.1.1 和 OpenSBI v0.4，容器名称沿用 Lab 1。

```bash
cd /Users/changluei/cd/nku/five/OS/os-labs
docker start os-labs-lab-1
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make clean && make'
docker exec -it os-labs-lab-1 sh -lc 'cd /workspace && make qemu'
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make grade'
```

`make grade` 使用课程原有工具，在临时副本中注入测试。正常启动需要另跑，以验证评分未覆盖的自动关机。`SHUTDOWN_AFTER` 按本组实现设为 10 次统计输出，即 1000 次时钟中断；指导书没有给出具体数值，报告明确说明这是本组取值。

Linux/WSL 安装同一课程工具链后，可直接在 `code/` 执行 `make clean && make`、`make qemu`、`make grade`。本地 `code/dev`、`code/environment/` 由 Git 排除，其中旧 `verify` 流程面向 Lab 1，不作为 Lab 3 验收入口。

## 截图与复现命令

三张截图已由崔迪于 2026-11-08 提供，报告中的第五章已插入原图并记录核对结果。启动图底部未完整显示关机，完整日志补充证明 10 次统计输出后自然退出；如需要完整关机画面，可在再次复现时另截终端末尾，不影响已有日志结论。

以下命令供后续复现和截图使用。先在本机终端执行：

```bash
cd /Users/changluei/cd/nku/five/OS/os-labs
mkdir -p report/logs report/images
docker start os-labs-lab-1
```

第一张：构建成功。截图应包含命令、编译/链接结果和两个生成文件。建议文件名 `2412439-lab3-build.png`。

```bash
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make clean && make && ls -lh bin/kernel bin/ucore.img' | tee report/logs/2412439-lab3-build.txt
```

第二张：正常启动与自动关机。截图应包含内存自检、时钟初始化、完整 10 行 `100 ticks` 和自动退出后重新出现的 shell 提示符；可缩小终端字号容纳完整画面。建议文件名 `2412439-lab3-boot.png`。

```bash
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make qemu' | tee report/logs/2412439-lab3-boot.txt
```

第三张：课程评分。截图应包含三项 PASS（35/35、25/25、40/40）及 `Total Score: 100/100`；可在终端回滚查看三项结果。建议文件名 `2412439-lab3-grade.png`。

```bash
docker exec os-labs-lab-1 sh -lc 'cd /workspace && make grade' | tee report/logs/2412439-lab3-grade.txt
rg 'PASS|FAIL|Total Score' report/logs/2412439-lab3-grade.txt
```

请使用本次真实终端画面，保留执行命令及关键结果。历史 Lab 2 图片不用于本次截图；目前不要求额外采集自编异常测试画面。

## 报告与证据

- `report/report.md`：Lab 3 正式报告，含三张已核对的真实截图。
- `report/prompt.md`：按成员整理的四段式提示词。
- `report/logs/2412819-lab3-tests.txt`：2026-11-04 的构建、课程评分、真实时钟/关机及补充检查记录。
- `report/logs/20261108-recheck-*.txt`：当前 Docker 环境的课程构建、运行及评分复验记录。
- `report/logs/2412852-exceptions-*.txt`：此前的异常补充测试历史记录。
- `report/images/2412439-lab3-*.png`：本次构建、启动与课程评分原图；`report/images/README.md` 记录核对结果及证据范围。
- `report/archive/`：Lab 2 正文归档，历史图片单独索引。

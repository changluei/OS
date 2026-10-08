# 操作系统实验仓库

每次实验使用独立分支：`lab1`、`lab2`、`lab3` 等。
`main` 保留初始代码和仓库使用说明；实验交付物请切换到对应的 `labx` 分支查看。

## 实验分支目录

```text
code/                  实验代码（在此目录执行 make）
report/report.md       按课程模板编写的实验报告
report/prompt.md       本实验使用的全部提示词
report/images/         报告中引用的测试截图
```

## 本地使用

本地仓库目录名为 `os-labs`，目录名与 Git 分支名互相独立。

```bash
git switch lab1
cd code
./dev setup
./dev build
```

提交时回到仓库根目录，确认内容后提交并上传：

```bash
git status
git add code report
git commit -m "更新 lab1 代码和报告"
git push
```

## 创建下一次实验

先提交当前修改。如果下一次实验延续实验一的实现：

```bash
git switch lab1
git switch -c lab2
```

将 `code/` 更新为实验二代码，将 `report/` 中的报告、提示词和截图更新为实验二内容。
如课程提供独立初始代码，应使用该实验的初始代码，并避免复制其他仓库的 `.git` 目录。

```bash
git add code report
git commit -m "完成 lab2"
git push -u origin lab2
```

后续提交仍使用该实验分支。无需将各实验分支合并到 `main`。
切换分支前先提交当前修改；Git 会随分支切换更新工作目录中的文件。

## 报告与提示词

报告使用课程提供的模板。模板中的示例、占位符和测试通过标记需要按实际情况修改，不能作为已完成实验的证明。
在 `prompt.md` 中按时间顺序保存原始提示词，包括后续修正提示词。
截图放在 `report/images/`，报告中使用相对路径，例如 `![测试结果](./images/test_result.png)`。

## Docker 实验环境（macOS / Linux）

本实验使用独立 Ubuntu 22.04 容器，宿主机编辑 `code/`，容器执行交叉编译、QEMU 和 GDB。
镜像从官方 Ubuntu 构建，并按宿主机选择架构，Apple Silicon 使用 arm64 Linux。
配置见 `code/environment/`，启动入口为 `code/dev`。需先启动 Docker Desktop。

```bash
cd code
./dev setup       # 首次安装工具并启动容器；以后也可以用来启动
./dev doctor      # 查看容器工具版本
./dev build       # 编译，生成 bin/kernel 和 bin/ucore.img
./dev qemu        # 运行；按 Ctrl+A，松开后按 X 退出 QEMU
./dev verify      # 自动验证编译、启动输出、GDB 和三个启动地址
./dev shell       # 进入容器，退出 shell 不会停止容器
./dev stop        # 停止并移除开发容器，保留本地代码和镜像
```

GDB 交互调试需要两个终端，均进入本地 `code/`：

```bash
# 终端一
./dev debug
# 终端二
./dev gdb
```

```text
(gdb) x/8i 0x1000
(gdb) info registers pc
(gdb) si
(gdb) b *0x80000000
(gdb) c
(gdb) b *kern_entry
(gdb) c
(gdb) info registers pc sp
(gdb) x/5i $pc
(gdb) si
```

GDB 在同一容器内连接 QEMU，主机调试端口仅映射到 `127.0.0.1:1234`。
容器安装 `gdb-multiarch`；Makefile 的 gdb 目标支持 `GDB` 覆盖，`./dev gdb` 已传入此参数。
`./dev make clean` 可以清理构建产物；`obj/` 和 `bin/` 已被 Git 忽略。
当前代码没有 `tools/grade.sh`，不要把 `make grade` 不可用写成环境安装失败，也不能声称评分测试已通过。

## Lab 1 分工建议（三人，姓名待确认）

本次重点是最小内核启动原理与调试，必做练习只有两个，没有单独列出的 Challenge。
无需把页表、物理内存分配或中断处理当成本次待实现任务。

| 成员 | 主责 | 具体交付 | 建议互审 |
|---|---|---|---|
| A | 练习 1、入口与链接 | 解释 `la sp, bootstacktop`、`tail kern_init`；分析栈、链接地址、ELF 与镜像、`.bss` 初始化；编写对应报告 | B 检查指令与地址分析 |
| B | 练习 2、GDB 启动跟踪 | 从 `0x1000` 跟踪到 `0x80000000`、`0x80200000`；记录反汇编、PC、SP、断点和截图；回答复位指令功能 | A 复核调试结果 |
| C | 构建与输出链、报告整合 | 验证构建与启动；分析 `cprintf → 格式化输出 → cons_putc → sbi_console_putchar → ecall`；整合环境、逻辑主线、知识点、提示词和提交目录 | A、B 共同审阅最终报告 |

每人都记录自己实际使用的提示词和结果，最后按时间顺序汇总到 `report/prompt.md`。
三人都应亲手完成一次构建、QEMU 启动和 GDB 连接；主责划分不替代个人理解。
协作开发建议各自 clone 同一仓库，切换到 `lab1`，先 `git pull --ff-only` 再编辑负责的文件，提交后 push。
不要同时修改报告同一段；由 C 最后整合，发生冲突时保留并核对各人的内容。

## Lab 1 待办

- [ ] 确认全体成员姓名、学号及 A/B/C 对应关系。
- [ ] 每人在自己的机器运行 Docker 环境并完成启动验证。
- [ ] A 完成练习 1，结合实际反汇编解释伪指令展开、栈向低地址增长、tail 不建立普通返回链的原因。
- [ ] A 梳理链接脚本的入口、段布局、4 KiB 对齐、`edata/end` 和 `.bss` 清零。
- [ ] B 交互完成练习 2，保存复位 ROM 指令、OpenSBI 入口、内核入口、设置栈指针前后的观察与截图。
- [ ] B 回答加电最初指令地址及功能，区分 QEMU 复位 ROM 和 OpenSBI。
- [ ] C 说明 ELF 调试文件与原始二进制镜像的用途，梳理编译、链接、objcopy、QEMU 流程。
- [ ] C 分析 SBI 输出链和 S 模式通过 ecall 请求 M 模式服务的过程。
- [ ] 补全报告中的实际环境、功能分析、两个练习答案、OS 知识点对应与未涉及知识点。
- [ ] 清理模板示例和不真实的测试通过标记；填入实际日期与分工。
- [ ] 汇总全部原始提示词，包含失败迭代和修正记录。
- [ ] 将真实截图放入 `report/images/`，在报告中引用并检查路径。
- [ ] 核实课程是否另行提供评分脚本；当前版本缺少 `tools/grade.sh`。
- [ ] 全组审阅代码、报告、提示词、截图，在 `lab1` 提交并推送。

指导书细节需要以代码和实测为准：原始命令的 `-device loader` 会在 CPU 执行前装载镜像；当前兼容配置使用 `-kernel`，同样在启动前装载镜像，因此
`watch *0x80200000` 不一定触发，不应为了得到“内核加载瞬间”而一直等待。
`0x1000` 是 QEMU virt 的复位 ROM，OpenSBI 的入口位于 `0x80000000`。
指导书项目组成中列出的部分文件属于更完整版本，当前最小代码不包含它们并不意味着遗漏了本次必做题。

阅读依据：本地指导书 `lab0/3_startdash.html`、`lab0/softwares.html`、
Lab 1 全章，尤其 `lab1_2_1_exercise.html` 与 `lab1_5_requirement.html`。
交付路径以教师最新要求为准：仓库根目录 `code/` 和 `report/`，报告为 `report/report.md`。
软件参考：[Ubuntu RISC-V GCC 包](https://packages.ubuntu.com/jammy/gcc-riscv64-unknown-elf)、
[QEMU virt 文档](https://www.qemu.org/docs/master/system/riscv/virt.html)、
[QEMU loader 文档](https://www.qemu.org/docs/master/system/generic-loader.html)。

### 启动参数兼容修正

Ubuntu 22.04 的 QEMU 6.2 使用内置 OpenSBI v0.9。原 Makefile 只用 `-device loader`
装载内核时，实测 OpenSBI 的 `Domain0 Next Address` 为 `0x0`，无法进入内核。
已将 `qemu` 和 `debug` 目标改为 `-kernel $(UCOREIMG)`，让 QEMU 正确设置下一阶段入口。
内核链接地址仍为 `0x80200000`，无需修改入口汇编或 SBI 输出实现。

### 本机已完成的环境验证（2026-10-08）

- [x] 构建并启动原生 ARM64 Ubuntu Docker 开发容器。
- [x] 安装交叉编译器、binutils、QEMU 和 gdb-multiarch。
- [x] 编译生成 RISC-V ELF 与内核镜像，确认入口为 `0x80200000`。
- [x] QEMU 输出 `(THU.CST) os is loading ...`。
- [x] GDB 实测停在 `0x1000`、`0x80000000` 和 `kern_entry (0x80200000)`。

完整环境日志：`code/environment/verification.txt`。
以上验证完成的是开发环境，练习答案、组员操作截图、正式报告和评分测试仍按待办推进。

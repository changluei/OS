# Lab 2 真实终端截图

用户于 2026-10-25 提供的五张原始 PNG，已收入 [主报告第五章](../report.md#五测试与验证)。以下文件均按附件原始字节复制，未重绘、裁剪或缩放。第五张采用完整长图，打开文件可按原始分辨率查看。

| 图号 | 文件 | 原始分辨率 | 画面与核对结果 |
|------|------|------------|----------------|
| 1 | [2412439-final-build.png](2412439-final-build.png) | 1212 × 898 | 清理后编译、链接及镜像生成成功；ELF 129K、原始镜像 29K，正常 |
| 2 | [2412439-final-boot.png](2412439-final-boot.png) | 1194 × 1566 | 128 MiB、默认 Best-Fit；SLUB 和页面分配自检成功，地址范围一致 |
| 3 | [2412439-final-grade.png](2412439-final-grade.png) | 2940 × 338 | page init、best fit 均 OK，课程评分 20/20 |
| 4 | [2412439-buddy-slub.png](2412439-buddy-slub.png) | 1194 × 1626 | Buddy 两轮初始空闲页均为 31919，页级和 SLUB 检查均成功，组合 PASS |
| 5 | [2412439-mmu-full.png](2412439-mmu-full.png) | 2932 × 5818 | QEMU 源码、页表、软件 TLB 和 guest 指令检查通过；含一条非致命 GDB 环境告警 |

## 输出说明

- 图 1 的 `bin/kernel` 是 ELF，`bin/ucore.img` 为 `objcopy --strip-all -O binary` 生成的镜像，文件大小不同正常。
- 图 2 与图 4 分别是默认 Best-Fit 内核和临时 Buddy 验证内核。内存占用、空闲起点及页表位置不同，不代表同一配置的结果前后矛盾。
- 图 3 的课程评分只覆盖初始化和 Best-Fit；Buddy、SLUB 由其他截图及完整组合日志补充验证。评分注入和临时构建目录属于原课程脚本的正常流程。
- 图 5 的 `Error disabling address space randomization: Operation not permitted` 表示 GDB 未能关闭地址随机化，后续断点、单步与最终 PASS 完整，未阻断本次验证。`<optimized out>`、线程切换和调试器脱离信息也未显示测试失败。
- 图 5 使用临时 First-Fit 内核。Sv39 的 1 GiB 根叶子与软件 TLB 的 4 KiB 填充粒度含义不同；DTB 魔数字节 `d0 0d fe ed` 与小端加载值 `0xedfe0dd0` 相符。

## 对应复现命令

本机容器 `os-labs-lab-1` 挂载当前仓库的 `code/`。容器名沿用 Lab 1，当前运行内容为 Lab 2。

```bash
# 图 1
docker exec os-labs-lab-1 make clean
docker exec os-labs-lab-1 make
docker exec os-labs-lab-1 ls -lh bin/kernel bin/ucore.img

# 图 2：自检通过后，Ctrl+A、再 X 退出
docker exec -it os-labs-lab-1 make qemu

# 图 3
docker exec os-labs-lab-1 make grade

# 图 4
docker exec os-labs-lab-1 python3 tools/lab2_verify.py --backend buddy --ram 128

# 图 5：需要已有 QEMU 源码及带符号构建
docker exec os-labs-lab-1 python3 tools/debug_mmu.py --qemu-source /tmp/qemu
```

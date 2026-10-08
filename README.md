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
make
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

# DPE-MVS-fast 代码版本、数据与 worktree 布局

## 1. Git 仓库与分支职责

GitHub 仓库：

```text
git@github.com:tobyal/DPE-MVS-fast.git
https://github.com/tobyal/DPE-MVS-fast.git
```

当前采用“方法稳定分支 + 具体版本分支”的组织方式：

| 分支 | 本地位置 | 用途 |
|---|---|---|
| `fast` | `/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast` | DPE-MVS-fast 方法的稳定/汇总分支 |
| `speed-optimization-v1` | `/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/speed-optimization-v1` | 速度优化版本一，独立编译和实验 |

当前不为 `main` 创建 worktree，也不把它纳入日常并行实验布局。`main` 分支仍保留在 Git 历史和远程仓库中，不删除。

这里不再使用 `perf/speed-optimization-v1` 作为日常版本名。具体版本直接使用 `speed-optimization-v1`，使分支名、worktree 目录名和实验版本名一致。

检查当前分支和全部 worktree：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
git branch --show-current
git status --short
git worktree list
```

预期结果：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast                                      [fast]
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/speed-optimization-v1       [speed-optimization-v1]
```

## 2. 代码目录统一布局

方法仓库保留一个稳定工作区，各个具体版本在统一的 worktree 父目录下平行存放：

```text
/mnt/sda/ubuntu/lkx/mvs/
├── DPE-MVS-fast/                              # fast 分支
├── DPE-MVS-fast-worktrees/
│   ├── speed-optimization-v1/                 # speed-optimization-v1 分支
│   ├── speed-optimization-v2/                 # 后续版本
│   └── <future-version>/                      # 其他后续版本
├── MVS_EXP/                                   # 所有版本共享的数据与结果
└── multi-view-evaluation/                     # ETH3D 官方评估工具
```

`DPE-MVS-fast-worktrees` 只是 worktree 的统一父目录，不是另一个 Git 仓库。里面每个版本目录都连接到同一个 DPE-MVS-fast Git 仓库，但拥有独立工作区和独立 `build/`。

当前速度优化版本一的创建方式：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
mkdir -p /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees

git worktree add \
  -b speed-optimization-v1 \
  /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/speed-optimization-v1 \
  fast
```

以后从 `fast` 创建速度优化版本二：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast

git worktree add \
  -b speed-optimization-v2 \
  /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/speed-optimization-v2 \
  fast
```

查看全部 worktree：

```bash
git worktree list
```

不再需要某个 worktree 时，应先确认其中没有未提交修改，再从稳定工作区执行：

```bash
git worktree remove /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/<version>
git worktree prune
```

## 3. 共享数据位置

所有分支和 worktree 共用仓库外部的数据根目录：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP
```

目录约定：

```text
MVS_EXP/
├── datasets/
│   └── ETH3D/
│       ├── raw/
│       │   ├── train/                    # ETH3D 训练场景原始数据
│       │   └── test/                     # ETH3D 测试场景原始数据
│       ├── processed/
│       │   └── <scene>/
│       │       ├── scale_1/
│       │       │   ├── images/
│       │       │   ├── cams/
│       │       │   └── pair.txt
│       │       ├── scale_2/
│       │       └── scale_4/
│       └── gt/
│           └── <scene>/<scene>/dslr_scan_eval/
│               ├── scan_alignment.mlp
│               └── scan*.ply
├── results/
│   ├── DPE-MVS/                          # 原方法结果
│   ├── DPE-MVS-fast/                     # fast 方法及其各版本结果
│   ├── SD-MVS/
│   └── Ours/
└── evaluations/
    ├── ETH3D/
    └── tmp/
        ├── summary.tsv
        └── <method>__<scene>__<experiment>/
```

DPE 输入必须位于：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/<scene>/<scale>/
```

每个输入目录至少需要：

```text
images/
cams/
pair.txt
```

ETH3D 官方 GT 必须位于：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/gt/<scene>/<scene>/dslr_scan_eval/scan_alignment.mlp
```

重建结果按照方法、场景和实验名称保存：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/<method>/<scene>/<experiment>/
```

当前两个 Git 分支都属于 `DPE-MVS-fast` 方法，因此都写入：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/<scene>/<experiment>/
```

为了区分版本并避免并行写入冲突，实验名应包含版本。例如：

```text
fast_half_20260915_120000
speed_optimization_v1_half_20260915_120000
```

官方评估工具位于：

```text
/mnt/sda/ubuntu/lkx/mvs/multi-view-evaluation
```

## 4. 多版本并行运行注意事项

- `fast` 与 `speed-optimization-v1` 是两个不同分支，可以同时签出和运行。
- 每个 worktree 在自己的目录中单独执行 CMake 配置与编译，不共享 `build/`。
- 所有版本共享输入数据和 GT，不需要复制数据集。
- 同一方法下的不同版本必须使用不同实验名，防止写入同一结果目录。
- 多个版本同时运行时，应显式分配不同 GPU，避免显存和计算资源争抢。
- 不要把 `MVS_EXP`、数据集、点云结果或 `DPE-MVS-fast-worktrees` 提交到 Git 仓库。
- `main` 暂时不参与当前布局；需要时仍可从远程或本地分支单独签出。

完整的编译、运行、并行场景和评估命令见：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast/RUN_MVS_EXP.md
```

# DPE-MVS-fast 仓库、数据与 worktree 布局

## 1. 当前版本

GitHub 仓库：

```text
git@github.com:tobyal/DPE-MVS-fast.git
https://github.com/tobyal/DPE-MVS-fast.git
```

当前工作分支：

```text
perf/speed-optimization-v1
```

版本用途：DPE-MVS 速度优化版本一。

当前工作区：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
```

检查当前工作区和分支：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
git branch --show-current
git status --short
git worktree list
```

## 2. 共享数据位置

所有 Git 分支和 worktree 共用仓库外部的数据根目录：

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
│   └── <method>/<scene>/<experiment>/
│       ├── DPE.ply
│       ├── views/
│       ├── config.txt
│       ├── runtime.txt
│       └── run.log
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

官方评估工具位于：

```text
/mnt/sda/ubuntu/lkx/mvs/multi-view-evaluation
```

## 3. 多个 Git 版本并行运行

推荐使用 `git worktree`。每个版本拥有独立的源码目录和 `build/`，但共用同一份 `MVS_EXP` 数据，适合同时编译、运行和比较多个分支，且不需要反复切换分支或复制整个仓库。

建议把额外工作区放在主仓库的同级目录，而不是放入仓库内部：

```text
/mnt/sda/ubuntu/lkx/mvs/
├── DPE-MVS-fast/                         # perf/speed-optimization-v1
├── DPE-MVS-fast-worktrees/
│   ├── main/                             # main
│   └── speed-optimization-v2/            # 后续性能版本
├── MVS_EXP/                              # 所有版本共享的数据与结果
└── multi-view-evaluation/                # ETH3D 官方评估工具
```

为 `main` 创建一个独立工作区：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
mkdir -p /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees
git worktree add /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/main main
```

以后从速度优化版本一创建速度优化版本二：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
git worktree add \
  -b perf/speed-optimization-v2 \
  /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/speed-optimization-v2 \
  perf/speed-optimization-v1
```

查看全部 worktree：

```bash
git worktree list
```

不再需要某个 worktree 时，应先确认其中没有未提交修改，再从主仓库执行：

```bash
git worktree remove /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast-worktrees/<name>
git worktree prune
```

## 4. 并行实验注意事项

- 同一个 Git 分支不能同时签出到两个 worktree；不同版本应使用不同分支。
- 每个 worktree 在自己的目录中单独执行 CMake 配置与编译，不共享 `build/`。
- 数据可以共享，但不同版本必须使用不同实验名，防止同时写入同一个结果目录。
- 推荐实验名包含版本，例如 `speed_v1_half_20260915_120000` 和 `main_half_20260915_120000`。
- 多个版本同时运行时，应显式分配不同 GPU，避免显存和计算资源争抢。
- 不要把 `MVS_EXP`、数据集、点云结果或 worktree 目录提交到 Git 仓库。

完整的编译、运行、并行场景和评估命令见：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast/RUN_MVS_EXP.md
```

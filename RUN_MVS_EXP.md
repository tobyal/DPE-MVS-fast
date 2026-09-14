# DPE-MVS-fast：MVS_EXP 编译、数据与运行手册

本文档合并并替代原来的 `RUN_PIPE_DPE.md` 和 `RUN_MVS_EXP.txt`。当前推荐方式是直接运行编译后的 C++/CUDA 程序，不使用 Python 启动器。

## 1. 当前目录约定

工程目录：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
```

实验根目录：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP
```

ETH3D 原始数据：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/raw/train/<scene>
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/raw/test/<scene>
```

DPE 输入数据：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/<scene>/<scale>
```

其中 `<scale>` 为：

- `scale_1`：全分辨率；
- `scale_2`：二分之一分辨率；
- `scale_4`：四分之一分辨率。

实验输出：

```text
/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/<scene>/<experiment>
```

每个输入目录必须包含：

```text
images/
cams/
pair.txt
```

输入目录只提供图像、相机和视图关系；运行缓存、中间图像、深度图和点云全部写入独立实验目录。

## 2. 激活环境

```bash
source /home/ubuntu/anaconda3/etc/profile.d/conda.sh
conda activate lkx_torch2.1
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
```

## 3. Release 模式编译

第一次编译、CMake 配置发生变化或源码修改后执行：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 16
```

如果 `build/` 已完成配置且只修改了源码，可以只执行：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
cmake --build build -j 16
```

编译后的程序为：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast/build/DPE
```

## 4. pipes 常规运行：不生成中间 JPG

输入为 `pipes/scale_2`，结果保存为 `half_fast_v2`：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
set -o pipefail

mkdir -p /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2

./build/DPE \
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/pipes/scale_2 \
  0 \
  --output=/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2 \
  --vis=none \
  --checkpoint=final \
  --profile=on \
  2>&1 | tee /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2/run.log
```

参数含义：

- `0`：使用 GPU 0；
- `--output`：指定独立实验输出目录；
- `--vis=none`：不生成中间 JPG；
- `--checkpoint=final`：只保存最终二进制状态；
- `--profile=on`：在日志末尾输出各阶段耗时；
- `set -o pipefail`：使用 `tee` 时仍能保留 DPE 的失败退出状态。

## 5. pipes 运行：生成所有中间图像

中间图像分析应使用一个新的实验名，避免复用已有边缘缓存。以下示例使用 `half_fast_v2_vis`：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
set -o pipefail

mkdir -p /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_vis

./build/DPE \
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/pipes/scale_2 \
  0 \
  --output=/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_vis \
  --vis=all \
  --checkpoint=final \
  --profile=on \
  2>&1 | tee /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_vis/run.log
```

`--vis=all` 为每个视图、每个尺度和每轮处理生成：

```text
views/00000000/depth_<iteration>.jpg
views/00000000/normal_<iteration>.jpg
views/00000000/weak_<iteration>.jpg
views/00000000/rawedge_<scale>.jpg
views/00000000/connect_<scale>.jpg
```

对于当前包含 14 个视图、3 个内部处理尺度的 `pipes/scale_2`：

- depth、normal、weak：`14 × 12 × 3 = 504` 张 JPG；
- rawedge、connect：最多约 `14 × 3 × 2 = 84` 张 JPG。

如果复用已有实验目录，程序会直接读取已存在的 edge/label 缓存，可能不会重新生成 `rawedge` 和 `connect` 图像。因此，可视化实验应使用新的输出目录。

## 6. pipes 运行：只生成最终一轮图像

如果只需要最终深度、法线和弱纹理状态图：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast
set -o pipefail

mkdir -p /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_final_vis

./build/DPE \
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/pipes/scale_2 \
  0 \
  --output=/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_final_vis \
  --vis=final \
  --checkpoint=final \
  --profile=on \
  2>&1 | tee /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/pipes/half_fast_v2_final_vis/run.log
```

`--vis=final` 会为每个视图保存最终的 depth、normal、weak JPG，但不会生成每个尺度的 `rawedge/connect` JPG。

## 7. 输出目录结构

```text
<experiment>/
├── DPE.ply
├── run.log
└── views/
    ├── 00000000/
    │   ├── depths.dmb
    │   ├── normals.dmb
    │   ├── weak.bin
    │   ├── selected_views.bin
    │   ├── edges_0.dmb
    │   ├── labels_0.dmb
    │   └── 中间 JPG（由 --vis 控制）
    ├── 00000001/
    └── ...
```

关键文件：

- 最终点云：`<experiment>/DPE.ply`；
- 完整终端日志：`<experiment>/run.log`；
- 每视图最终状态：`<experiment>/views/<view_id>/`；
- 中间可视化：`<experiment>/views/<view_id>/*.jpg`。

## 8. 其他场景和尺度

运行其他场景时，同时修改输入路径、输出路径中的场景名和实验名。例如运行 `courtyard/scale_2`：

```bash
mkdir -p /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/courtyard/half_fast_v1

./build/DPE \
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/courtyard/scale_2 \
  0 \
  --output=/mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/courtyard/half_fast_v1 \
  --vis=none \
  --checkpoint=final \
  --profile=on \
  2>&1 | tee /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/courtyard/half_fast_v1/run.log
```

建议实验命名：

| 输入尺度 | 建议名称 |
|---|---|
| `scale_1` | `full_fast_v1` |
| `scale_2` | `half_fast_v1` |
| `scale_4` | `quarter_fast_v1` |

## 9. 多场景并行运行

服务器当前有 4 张 RTX A6000。推荐每张 GPU 同时运行一个场景；这能让不同场景使用独立 CUDA 设备，避免同一 GPU 上多个 DPE 进程争抢计算资源。

并行脚本：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast/run_parallel_scenes.sh
```

使用 4 张 GPU 并行运行多个 `scale_2` 场景：

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast

./run_parallel_scenes.sh \
  --scale scale_2 \
  --experiment half_fast_v3 \
  --gpus 0,1,2,3 \
  --vis none \
  --checkpoint final \
  --profile on \
  pipes courtyard office facade kicker meadow
```

```bash
cd /mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast

./run_parallel_scenes.sh \
  --scale scale_2 \
  --experiment half_fast_v3 \
  --gpus 0,1,2,3 \
  --vis final \
  --checkpoint final \
  --profile on \
  courtyard delivery_area electro facade kicker meadow office pipes \
  playground relief relief_2 terrace terrains
```

上述命令会建立 4 个 worker：

- 第一个场景分配到 GPU 0；
- 第二个场景分配到 GPU 1；
- 第三个场景分配到 GPU 2；
- 第四个场景分配到 GPU 3；
- 某个 worker 完成当前场景后，再继续运行分配给它的后续场景。

每个场景输出到独立目录：

```text
MVS_EXP/results/DPE-MVS-fast/<scene>/half_fast_v3/
├── DPE.ply
├── config.txt
├── runtime.txt
├── run.log
└── views/
```

正式启动前可以先做路径和任务分配检查，不执行计算：

```bash
./run_parallel_scenes.sh \
  --scale scale_2 \
  --experiment half_fast_v3 \
  --gpus 0,1,2,3 \
  --dry-run \
  pipes courtyard office facade
```

默认不允许写入非空实验目录。如果确认需要续写或覆盖同名文件，必须显式增加：

```text
--allow-existing
```

不建议默认在同一张 GPU 上运行多个场景。如果确实要测试单卡多进程，可以显式设置：

```bash
./run_parallel_scenes.sh \
  --scale scale_2 \
  --experiment half_fast_parallel_test \
  --gpus 0 \
  --workers-per-gpu 2 \
  pipes courtyard
```

单卡多进程可能导致：

- 显存不足；
- PatchMatch kernel 相互争抢；
- 单场景耗时显著增加；
- profile 结果无法作为独占 GPU 基准。

生成所有场景的中间图像时可使用 `--vis all`，但会产生大量 JPG 和额外磁盘 I/O。批量基准测试建议使用 `--vis none`。

查看脚本完整帮助：

```bash
./run_parallel_scenes.sh --help
```

## 10. 从 ETH3D raw 转换输入：旧文档内容合并说明

原 `RUN_PIPE_DPE.md` 记录了通过 `colmap2mvsnet_acm.py` 将 ETH3D 原始数据转换成 DPE 输入格式的流程。当前对应关系已经调整为：

```text
原始场景：MVS_EXP/datasets/ETH3D/raw/train/pipes
处理结果：MVS_EXP/datasets/ETH3D/processed/pipes/scale_2
```

转换脚本仍位于：

```text
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast/colmap2mvsnet_acm.py
```

该脚本要求输入场景中存在名为 `sparse/` 的 COLMAP 模型，而 ETH3D 原始场景使用：

```text
dslr_calibration_undistorted/
```

旧文档通过复制后将它命名为 `sparse/`。当前 raw 和 processed 已完成复制与整理，正常运行 DPE 时不需要再次执行转换。若以后需要重新转换，建议在独立临时工作目录中准备 `sparse/`，再把生成的 `images/`、`cams/` 和 `pair.txt` 复制到目标 `processed/<scene>/<scale>/`；不要删除或直接修改 `raw/` 原始数据。

原文档中的以下路径已经废弃，不应继续作为新实验路径：

```text
/mnt/sda/ubuntu/lkx/mvs/datasets/ETH3D_DPE/...
/mnt/sda/ubuntu/lkx/mvs/DPE-MVS/DPE-MVS/...
```

旧文档中的 `rm -rf` 转换命令没有合并到新手册，以避免误删已有数据。

## 11. 如何判断运行是否成功

正常结束时日志应包含：

```text
[DPE Progress] patchmatch ... 100.0%
[DPE Progress] fusion done; point cloud saved to .../DPE.ply
[DPE Profile Summary]
All done
```

如果出现以下内容，则说明运行异常：

```text
CUDA error
Segmentation fault
core dumped
out of memory
```

多尺度切换时出现以下提示属于正常状态缩放，不是错误：

```text
Weak info doesn't match the images' size!
Depth and Normal doesn't match the images' size!
Select view doesn't match the images' size!
Scale done
```

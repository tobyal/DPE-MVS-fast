#!/usr/bin/env python3
"""DPE-MVS-fast 在 MVS_EXP 目录中的推荐启动脚本。

目录约定
--------
输入目录：
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/datasets/ETH3D/processed/<scene>/<scale>

输出目录：
  /mnt/sda/ubuntu/lkx/mvs/MVS_EXP/results/DPE-MVS-fast/<scene>/<experiment>

示例 1：只保存最终结果，不生成中间 JPG
  python3 run_mvs_exp.py --scene pipes --scale scale_2 \
      --experiment half_fast_v2 --vis none

示例 2：生成每个尺度、每轮迭代的中间图像
  python3 run_mvs_exp.py --scene pipes --scale scale_2 \
      --experiment half_fast_v2_vis --vis all

示例 3：只生成最终一轮的 depth/normal/weak 图像
  python3 run_mvs_exp.py --scene pipes --scale scale_2 \
      --experiment half_fast_v2_final_vis --vis final

中间图像位于：
  <output>/views/00000000/depth_<iteration>.jpg
  <output>/views/00000000/normal_<iteration>.jpg
  <output>/views/00000000/weak_<iteration>.jpg

当 --vis=all 且使用一个全新的实验目录时，还会生成各尺度的边缘图：
  rawedge_<scale>.jpg 和 connect_<scale>.jpg

注意：不要为了生成中间图像复用已有实验目录。边缘缓存已经存在时，程序会
直接复用缓存，不会再次生成对应的 rawedge/connect JPG。建议为可视化运行使用
新的 experiment 名称。
"""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_EXP_ROOT = Path("/mnt/sda/ubuntu/lkx/mvs/MVS_EXP")
DEFAULT_PROJECT = Path("/mnt/sda/ubuntu/lkx/mvs/DPE-MVS-fast")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="按 MVS_EXP 目录规范运行 DPE-MVS-fast，并记录日志和配置。"
    )
    parser.add_argument("--scene", required=True, help="ETH3D 场景名，例如 pipes")
    parser.add_argument(
        "--scale",
        required=True,
        choices=("scale_1", "scale_2", "scale_4"),
        help="选择 processed 下的数据尺度",
    )
    parser.add_argument(
        "--experiment",
        required=True,
        help="实验目录名，例如 half_fast_v2；建议每次使用新名称",
    )
    parser.add_argument("--gpu", type=int, default=0, help="CUDA GPU 编号，默认 0")
    parser.add_argument(
        "--vis",
        choices=("none", "final", "all"),
        default="none",
        help="none=不写 JPG；final=只写最终一轮；all=写全部中间图像",
    )
    parser.add_argument(
        "--checkpoint",
        choices=("none", "final", "all"),
        default="final",
        help="控制深度、法线等二进制 checkpoint 的写入频率",
    )
    parser.add_argument(
        "--profile", choices=("on", "off"), default="on", help="是否输出阶段耗时"
    )
    parser.add_argument("--exp-root", type=Path, default=DEFAULT_EXP_ROOT)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument(
        "--allow-existing",
        action="store_true",
        help="允许写入非空实验目录；默认拒绝，以免混合两次实验结果",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="只显示输入、输出和命令，不运行"
    )
    return parser.parse_args()


def validate_input(input_dir: Path) -> None:
    required = (input_dir / "images", input_dir / "cams", input_dir / "pair.txt")
    missing = [str(item) for item in required if not item.exists()]
    if missing:
        raise FileNotFoundError("输入目录不完整，缺少：\n  " + "\n  ".join(missing))


def main() -> int:
    args = parse_args()
    input_dir = (
        args.exp_root / "datasets" / "ETH3D" / "processed" / args.scene / args.scale
    )
    output_dir = (
        args.exp_root
        / "results"
        / "DPE-MVS-fast"
        / args.scene
        / args.experiment
    )
    binary = args.project / "build" / "DPE"

    validate_input(input_dir)
    if not binary.is_file():
        raise FileNotFoundError(f"找不到已编译程序：{binary}")
    if output_dir.exists() and any(output_dir.iterdir()) and not args.allow_existing:
        raise FileExistsError(
            f"实验目录非空：{output_dir}\n"
            "请换一个 --experiment 名称，或明确传入 --allow-existing。"
        )

    command = [
        str(binary),
        str(input_dir),
        str(args.gpu),
        f"--output={output_dir}",
        f"--vis={args.vis}",
        f"--checkpoint={args.checkpoint}",
        f"--profile={args.profile}",
    ]
    command_text = shlex.join(command)
    print(f"输入目录：{input_dir}")
    print(f"输出目录：{output_dir}")
    print(f"运行命令：{command_text}")
    if args.dry_run:
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)
    config_path = output_dir / "config.txt"
    config_path.write_text(
        "\n".join(
            (
                f"scene={args.scene}",
                f"scale={args.scale}",
                f"experiment={args.experiment}",
                f"gpu={args.gpu}",
                f"vis={args.vis}",
                f"checkpoint={args.checkpoint}",
                f"profile={args.profile}",
                f"input={input_dir}",
                f"output={output_dir}",
                f"command={command_text}",
                "",
            )
        ),
        encoding="utf-8",
    )

    start = time.monotonic()
    log_path = output_dir / "run.log"
    with log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.Popen(
            command,
            cwd=args.project,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            log_file.write(line)
        return_code = process.wait()

    elapsed = time.monotonic() - start
    (output_dir / "runtime.txt").write_text(
        f"wall_time_seconds={elapsed:.3f}\nexit_code={return_code}\n",
        encoding="utf-8",
    )
    print(f"运行结束：exit_code={return_code}, wall_time={elapsed:.3f}s")
    print(f"点云位置：{output_dir / 'DPE.ply'}")
    print(f"视图结果：{output_dir / 'views'}")
    return return_code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, FileExistsError, ValueError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        raise SystemExit(2)

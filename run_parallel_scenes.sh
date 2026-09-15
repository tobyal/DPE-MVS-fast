#!/usr/bin/env bash
# 在多张 GPU 上并行运行多个 ETH3D 场景。
# 默认每张 GPU 只启动一个 DPE 进程；同一 GPU 多进程必须显式设置。

set -uo pipefail

EXP_ROOT="/mnt/sda/ubuntu/lkx/mvs/MVS_EXP"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$PROJECT_DIR/build/DPE"
SCALE="scale_2"
EXPERIMENT=""
EXPERIMENT_AUTO=0
GPU_CSV="0"
WORKERS_PER_GPU=1
VIS="none"
CHECKPOINT="final"
PROFILE="on"
ALLOW_EXISTING=0
DRY_RUN=0
SCENES=()

usage() {
    cat <<'EOF'
用法：
  ./run_parallel_scenes.sh [选项] scene1 scene2 ...

常用选项：
  --experiment NAME       实验名称；省略时自动使用 YYYYMMDD_HHMMSS 时间戳
  --scale SCALE           scale_1、scale_2 或 scale_4；默认 scale_2
  --gpus LIST             GPU 编号，逗号分隔；默认 0，例如 0,1,2,3
  --workers-per-gpu N     每张 GPU 的并发进程数；默认 1
  --vis MODE              none、final 或 all；默认 none
  --checkpoint MODE       none、final 或 all；默认 final
  --profile MODE          on 或 off；默认 on
  --exp-root PATH         MVS_EXP 根目录
  --allow-existing        允许写入非空实验目录
  --dry-run               只检查并显示任务，不启动 DPE
  -h, --help              显示帮助

示例：
  ./run_parallel_scenes.sh \
    --scale scale_2 \
    --experiment half_fast_v3 \
    --gpus 0,1,2,3 \
    pipes courtyard office facade kicker meadow
EOF
}

require_value() {
    if [[ $# -lt 2 ]]; then
        echo "错误：$1 缺少参数。" >&2
        usage >&2
        exit 2
    fi
    if [[ -z "$2" ]]; then
        echo "错误：$1 的参数不能为空。" >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --scale)
            require_value "$@"; SCALE="$2"; shift 2 ;;
        --experiment)
            require_value "$@"; EXPERIMENT="$2"; shift 2 ;;
        --gpus)
            require_value "$@"; GPU_CSV="$2"; shift 2 ;;
        --workers-per-gpu)
            require_value "$@"; WORKERS_PER_GPU="$2"; shift 2 ;;
        --vis)
            require_value "$@"; VIS="$2"; shift 2 ;;
        --checkpoint)
            require_value "$@"; CHECKPOINT="$2"; shift 2 ;;
        --profile)
            require_value "$@"; PROFILE="$2"; shift 2 ;;
        --exp-root)
            require_value "$@"; EXP_ROOT="$2"; shift 2 ;;
        --allow-existing)
            ALLOW_EXISTING=1; shift ;;
        --dry-run)
            DRY_RUN=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do SCENES+=("$1"); shift; done ;;
        -* )
            echo "错误：未知选项 $1" >&2; usage >&2; exit 2 ;;
        *)
            SCENES+=("$1"); shift ;;
    esac
done

[[ "$SCALE" =~ ^scale_(1|2|4)$ ]] || { echo "错误：无效 scale：$SCALE" >&2; exit 2; }
[[ "$VIS" =~ ^(none|final|all)$ ]] || { echo "错误：无效 vis：$VIS" >&2; exit 2; }
[[ "$CHECKPOINT" =~ ^(none|final|all)$ ]] || { echo "错误：无效 checkpoint：$CHECKPOINT" >&2; exit 2; }
[[ "$PROFILE" =~ ^(on|off)$ ]] || { echo "错误：无效 profile：$PROFILE" >&2; exit 2; }
[[ "$WORKERS_PER_GPU" =~ ^[1-9][0-9]*$ ]] || {
    echo "错误：--workers-per-gpu 必须是正整数。" >&2; exit 2;
}
[[ ${#SCENES[@]} -gt 0 ]] || { echo "错误：至少指定一个场景。" >&2; exit 2; }
[[ -x "$BINARY" ]] || { echo "错误：找不到可执行程序：$BINARY" >&2; exit 2; }

# 整批任务只生成一次时间戳，保证所有场景进入同名实验目录。
if [[ -z "$EXPERIMENT" ]]; then
    EXPERIMENT="$(date +%Y%m%d_%H%M%S)"
    EXPERIMENT_AUTO=1
fi

IFS=',' read -r -a GPU_IDS <<< "$GPU_CSV"
[[ ${#GPU_IDS[@]} -gt 0 ]] || { echo "错误：GPU 列表为空。" >&2; exit 2; }
for gpu_id in "${GPU_IDS[@]}"; do
    [[ "$gpu_id" =~ ^[0-9]+$ ]] || { echo "错误：无效 GPU 编号：$gpu_id" >&2; exit 2; }
    nvidia-smi -i "$gpu_id" --query-gpu=index --format=csv,noheader >/dev/null || {
        echo "错误：GPU $gpu_id 不可用。" >&2; exit 2;
    }
done

# 先检查所有输入和输出，避免启动部分场景后才发现路径问题。
for scene in "${SCENES[@]}"; do
    input_dir="$EXP_ROOT/datasets/ETH3D/processed/$scene/$SCALE"
    output_dir="$EXP_ROOT/results/DPE-MVS-fast/$scene/$EXPERIMENT"
    for required_path in "$input_dir/images" "$input_dir/cams" "$input_dir/pair.txt"; do
        [[ -e "$required_path" ]] || { echo "错误：缺少输入 $required_path" >&2; exit 2; }
    done
    if [[ -d "$output_dir" ]] && [[ -n "$(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]] \
        && [[ $ALLOW_EXISTING -eq 0 ]]; then
        echo "错误：实验目录非空：$output_dir" >&2
        echo "请更换 --experiment，或明确使用 --allow-existing。" >&2
        exit 2
    fi
done

GPU_SLOTS=()
for gpu_id in "${GPU_IDS[@]}"; do
    for ((worker_index = 0; worker_index < WORKERS_PER_GPU; ++worker_index)); do
        GPU_SLOTS+=("$gpu_id")
    done
done

echo "项目：$PROJECT_DIR"
echo "实验根目录：$EXP_ROOT"
echo "尺度：$SCALE"
echo "实验名：$EXPERIMENT"
if [[ $EXPERIMENT_AUTO -eq 1 ]]; then
    echo "实验名来源：自动时间戳"
else
    echo "实验名来源：命令行指定"
fi
echo "GPU：$GPU_CSV"
echo "每张 GPU worker 数：$WORKERS_PER_GPU"
echo "总 worker 数：${#GPU_SLOTS[@]}"
echo "场景：${SCENES[*]}"

if [[ $WORKERS_PER_GPU -gt 1 ]]; then
    echo "警告：同一 GPU 将运行多个 DPE 进程，请自行确认显存容量和性能收益。" >&2
fi

for scene_index in "${!SCENES[@]}"; do
    slot_index=$((scene_index % ${#GPU_SLOTS[@]}))
    echo "任务：${SCENES[$scene_index]} -> GPU ${GPU_SLOTS[$slot_index]} / slot $slot_index"
done

[[ $DRY_RUN -eq 1 ]] && exit 0

run_scene() {
    local scene="$1"
    local gpu_id="$2"
    local input_dir="$EXP_ROOT/datasets/ETH3D/processed/$scene/$SCALE"
    local output_dir="$EXP_ROOT/results/DPE-MVS-fast/$scene/$EXPERIMENT"
    local start_epoch end_epoch return_code

    mkdir -p "$output_dir"
    {
        echo "scene=$scene"
        echo "scale=$SCALE"
        echo "experiment=$EXPERIMENT"
        echo "gpu=$gpu_id"
        echo "vis=$VIS"
        echo "checkpoint=$CHECKPOINT"
        echo "profile=$PROFILE"
        echo "input=$input_dir"
        echo "output=$output_dir"
        echo "binary=$BINARY"
    } > "$output_dir/config.txt"

    echo "[$scene] 开始：GPU $gpu_id"
    start_epoch=$(date +%s)
    "$BINARY" \
        "$input_dir" \
        "$gpu_id" \
        "--output=$output_dir" \
        "--vis=$VIS" \
        "--checkpoint=$CHECKPOINT" \
        "--profile=$PROFILE" \
        2>&1 | tee "$output_dir/run.log" | sed -u "s/^/[$scene] /"
    return_code=${PIPESTATUS[0]}
    end_epoch=$(date +%s)

    {
        echo "wall_time_seconds=$((end_epoch - start_epoch))"
        echo "exit_code=$return_code"
        echo "gpu=$gpu_id"
    } > "$output_dir/runtime.txt"

    if [[ $return_code -eq 0 ]]; then
        echo "[$scene] 完成：$output_dir/DPE.ply"
    else
        echo "[$scene] 失败：exit_code=$return_code，检查 $output_dir/run.log" >&2
    fi
    return "$return_code"
}

run_worker() {
    local slot_index="$1"
    local gpu_id="${GPU_SLOTS[$slot_index]}"
    local scene_index
    local worker_status=0

    for ((scene_index = slot_index; scene_index < ${#SCENES[@]}; scene_index += ${#GPU_SLOTS[@]})); do
        run_scene "${SCENES[$scene_index]}" "$gpu_id" || worker_status=1
    done
    return "$worker_status"
}

PIDS=()
cleanup_workers() {
    local worker_pid
    for worker_pid in "${PIDS[@]}"; do
        kill "$worker_pid" 2>/dev/null || true
    done
}
trap cleanup_workers INT TERM

for slot_index in "${!GPU_SLOTS[@]}"; do
    run_worker "$slot_index" &
    PIDS+=("$!")
done

overall_status=0
for worker_pid in "${PIDS[@]}"; do
    wait "$worker_pid" || overall_status=1
done
trap - INT TERM

if [[ $overall_status -eq 0 ]]; then
    echo "全部场景运行成功。"
else
    echo "至少一个场景运行失败，请检查对应实验目录中的 runtime.txt 和 run.log。" >&2
fi
exit "$overall_status"

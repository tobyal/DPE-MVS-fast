#!/usr/bin/env bash
# 批量调用 ETH3D 官方 multi-view-evaluation。
# 对 methods × scenes × experiments 的全部组合逐一评估。

set -uo pipefail

EXP_ROOT="/mnt/sda/ubuntu/lkx/mvs/MVS_EXP"
EVALUATOR_ROOT="/mnt/sda/ubuntu/lkx/mvs/multi-view-evaluation"
TOLERANCES="0.01,0.02,0.05,0.1"
ALLOW_EXISTING=0
DRY_RUN=0
METHODS_CSV=""
SCENES_CSV=""
EXPERIMENTS_CSV=""

usage() {
    cat <<'EOF'
用法：
  ./evaluate_mvs.sh \
    --methods METHOD1,METHOD2 \
    --scenes SCENE1,SCENE2 \
    --experiments EXP1,EXP2 [选项]

必需参数：
  --methods LIST          方法名，多个名称用逗号分隔
  --scenes LIST           ETH3D 场景名，多个名称用逗号分隔
  --experiments LIST      实验名，多个名称用逗号分隔

可选参数：
  --tolerances LIST       容差列表；默认 0.01,0.02,0.05,0.1
  --exp-root PATH         MVS_EXP 根目录
  --evaluator-root PATH   官方评估工具目录
  --allow-existing        允许重新评估已有组合目录
  --dry-run               只检查并打印组合，不运行评估
  -h, --help              显示帮助

示例：
  ./evaluate_mvs.sh \
    --methods DPE-MVS,DPE-MVS-fast \
    --scenes pipes,courtyard \
    --experiments half_fast_v3
EOF
}

require_value() {
    if [[ $# -lt 2 || -z "$2" ]]; then
        echo "错误：$1 缺少参数。" >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --methods)
            require_value "$@"; METHODS_CSV="$2"; shift 2 ;;
        --scenes)
            require_value "$@"; SCENES_CSV="$2"; shift 2 ;;
        --experiments)
            require_value "$@"; EXPERIMENTS_CSV="$2"; shift 2 ;;
        --tolerances)
            require_value "$@"; TOLERANCES="$2"; shift 2 ;;
        --exp-root)
            require_value "$@"; EXP_ROOT="$2"; shift 2 ;;
        --evaluator-root)
            require_value "$@"; EVALUATOR_ROOT="$2"; shift 2 ;;
        --allow-existing)
            ALLOW_EXISTING=1; shift ;;
        --dry-run)
            DRY_RUN=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "错误：未知参数 $1" >&2
            usage >&2
            exit 2 ;;
    esac
done

[[ -n "$METHODS_CSV" ]] || { echo "错误：必须指定 --methods。" >&2; exit 2; }
[[ -n "$SCENES_CSV" ]] || { echo "错误：必须指定 --scenes。" >&2; exit 2; }
[[ -n "$EXPERIMENTS_CSV" ]] || { echo "错误：必须指定 --experiments。" >&2; exit 2; }
[[ "$TOLERANCES" =~ ^[0-9]+([.][0-9]+)?(,[0-9]+([.][0-9]+)?)*$ ]] || {
    echo "错误：--tolerances 必须是逗号分隔的非负数。" >&2
    exit 2
}

IFS=',' read -r -a METHODS <<< "$METHODS_CSV"
IFS=',' read -r -a SCENES <<< "$SCENES_CSV"
IFS=',' read -r -a EXPERIMENTS <<< "$EXPERIMENTS_CSV"

validate_name() {
    local kind="$1"
    local value="$2"
    [[ "$value" =~ ^[A-Za-z0-9._-]+$ ]] || {
        echo "错误：${kind} '${value}' 只能包含字母、数字、点、下划线和连字符。" >&2
        exit 2
    }
}

for method in "${METHODS[@]}"; do validate_name "方法名" "$method"; done
for scene in "${SCENES[@]}"; do validate_name "场景名" "$scene"; done
for experiment in "${EXPERIMENTS[@]}"; do validate_name "实验名" "$experiment"; done

EVALUATOR="$EVALUATOR_ROOT/build/ETH3DMultiViewEvaluation"
TMP_ROOT="$EXP_ROOT/evaluations/tmp"
SUMMARY="$TMP_ROOT/summary.tsv"

[[ -x "$EVALUATOR" ]] || { echo "错误：找不到评估程序：$EVALUATOR" >&2; exit 2; }

# 启动前检查全部笛卡尔组合，避免运行一部分后才发现输入缺失。
combination_count=0
for method in "${METHODS[@]}"; do
    for scene in "${SCENES[@]}"; do
        gt_mlp="$EXP_ROOT/datasets/ETH3D/gt/$scene/$scene/dslr_scan_eval/scan_alignment.mlp"
        [[ -f "$gt_mlp" ]] || { echo "错误：缺少 GT：$gt_mlp" >&2; exit 2; }
        for experiment in "${EXPERIMENTS[@]}"; do
            reconstruction="$EXP_ROOT/results/$method/$scene/$experiment/DPE.ply"
            combination_dir="$TMP_ROOT/${method}__${scene}__${experiment}"
            [[ -f "$reconstruction" ]] || {
                echo "错误：缺少重建点云：$reconstruction" >&2
                exit 2
            }
            if [[ -d "$combination_dir" ]] \
                && [[ -n "$(find "$combination_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]] \
                && [[ $ALLOW_EXISTING -eq 0 ]]; then
                echo "错误：评估目录非空：$combination_dir" >&2
                echo "如需重新评估，请增加 --allow-existing。" >&2
                exit 2
            fi
            combination_count=$((combination_count + 1))
        done
    done
done

echo "评估工具：$EVALUATOR"
echo "评估输出：$TMP_ROOT"
echo "汇总表：$SUMMARY"
echo "容差：$TOLERANCES"
echo "组合数：$combination_count"

for method in "${METHODS[@]}"; do
    for scene in "${SCENES[@]}"; do
        for experiment in "${EXPERIMENTS[@]}"; do
            echo "组合：${method} × ${scene} × ${experiment}"
        done
    done
done

[[ $DRY_RUN -eq 1 ]] && exit 0

mkdir -p "$TMP_ROOT"
success_count=0
failure_count=0

append_summary_rows() {
    local evaluated_at="$1"
    local method="$2"
    local scene="$3"
    local experiment="$4"
    local status="$5"
    local combination_dir="$6"
    shift 6
    local -a tolerances completeness accuracy f1
    read -r -a tolerances <<< "$1"
    read -r -a completeness <<< "$2"
    read -r -a accuracy <<< "$3"
    read -r -a f1 <<< "$4"

    exec 9>>"$SUMMARY"
    flock 9
    if [[ ! -s "$SUMMARY" ]]; then
        printf 'evaluated_at\tmethod\tscene\texperiment\ttolerance\tcompleteness\taccuracy\tf1\tstatus\tresult_dir\n' >&9
    fi
    if [[ "$status" == "success" ]]; then
        local index
        for index in "${!tolerances[@]}"; do
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$evaluated_at" "$method" "$scene" "$experiment" \
                "${tolerances[$index]}" "${completeness[$index]}" \
                "${accuracy[$index]}" "${f1[$index]}" "$status" "$combination_dir" >&9
        done
    else
        printf '%s\t%s\t%s\t%s\t\t\t\t\t%s\t%s\n' \
            "$evaluated_at" "$method" "$scene" "$experiment" "$status" "$combination_dir" >&9
    fi
    flock -u 9
    exec 9>&-
}

for method in "${METHODS[@]}"; do
    for scene in "${SCENES[@]}"; do
        gt_mlp="$EXP_ROOT/datasets/ETH3D/gt/$scene/$scene/dslr_scan_eval/scan_alignment.mlp"
        for experiment in "${EXPERIMENTS[@]}"; do
            reconstruction="$EXP_ROOT/results/$method/$scene/$experiment/DPE.ply"
            combination_name="${method}__${scene}__${experiment}"
            combination_dir="$TMP_ROOT/$combination_name"
            log_path="$combination_dir/evaluation.log"
            metrics_path="$combination_dir/metrics.tsv"
            evaluated_at="$(date '+%Y-%m-%dT%H:%M:%S%z')"

            mkdir -p "$combination_dir/accuracy" "$combination_dir/completeness"
            {
                echo "evaluated_at=$evaluated_at"
                echo "method=$method"
                echo "scene=$scene"
                echo "experiment=$experiment"
                echo "tolerances=$TOLERANCES"
                echo "reconstruction=$reconstruction"
                echo "ground_truth=$gt_mlp"
                echo "evaluator=$EVALUATOR"
                echo "output=$combination_dir"
            } > "$combination_dir/config.txt"

            echo "[$combination_name] 开始评估"
            "$EVALUATOR" \
                --reconstruction_ply_path "$reconstruction" \
                --ground_truth_mlp_path "$gt_mlp" \
                --tolerances "$TOLERANCES" \
                --accuracy_cloud_output_path "$combination_dir/accuracy/accuracy" \
                --completeness_cloud_output_path "$combination_dir/completeness/completeness" \
                2>&1 | tee "$log_path"
            return_code=${PIPESTATUS[0]}
            echo "return_code=$return_code" >> "$combination_dir/config.txt"

            if [[ $return_code -ne 0 ]]; then
                echo "[$combination_name] 失败，退出码：$return_code" >&2
                append_summary_rows "$evaluated_at" "$method" "$scene" "$experiment" \
                    "failed" "$combination_dir" "" "" "" ""
                failure_count=$((failure_count + 1))
                continue
            fi

            tolerance_line="$(awk -F': ' '/^Tolerances:/{value=$2} END{print value}' "$log_path")"
            completeness_line="$(awk -F': ' '/^Completenesses:/{value=$2} END{print value}' "$log_path")"
            accuracy_line="$(awk -F': ' '/^Accuracies:/{value=$2} END{print value}' "$log_path")"
            f1_line="$(awk -F': ' '/^F1-scores:/{value=$2} END{print value}' "$log_path")"
            read -r -a tolerance_values <<< "$tolerance_line"
            read -r -a completeness_values <<< "$completeness_line"
            read -r -a accuracy_values <<< "$accuracy_line"
            read -r -a f1_values <<< "$f1_line"

            value_count=${#tolerance_values[@]}
            if [[ $value_count -eq 0 \
                || ${#completeness_values[@]} -ne $value_count \
                || ${#accuracy_values[@]} -ne $value_count \
                || ${#f1_values[@]} -ne $value_count ]]; then
                echo "[$combination_name] 失败：无法从官方日志解析完整指标。" >&2
                append_summary_rows "$evaluated_at" "$method" "$scene" "$experiment" \
                    "parse_failed" "$combination_dir" "" "" "" ""
                failure_count=$((failure_count + 1))
                continue
            fi

            printf 'tolerance\tcompleteness\taccuracy\tf1\n' > "$metrics_path"
            for index in "${!tolerance_values[@]}"; do
                printf '%s\t%s\t%s\t%s\n' \
                    "${tolerance_values[$index]}" "${completeness_values[$index]}" \
                    "${accuracy_values[$index]}" "${f1_values[$index]}" >> "$metrics_path"
            done

            append_summary_rows "$evaluated_at" "$method" "$scene" "$experiment" \
                "success" "$combination_dir" \
                "${tolerance_values[*]}" "${completeness_values[*]}" \
                "${accuracy_values[*]}" "${f1_values[*]}"
            success_count=$((success_count + 1))
            echo "[$combination_name] 完成"
        done
    done
done

echo "评估完成：成功 $success_count，失败 $failure_count，总计 $combination_count。"
echo "汇总表：$SUMMARY"
[[ $failure_count -eq 0 ]]

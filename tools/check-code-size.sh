#!/usr/bin/env bash
# 代码体量护栏：硬规则 + 单文件 ratchet。
#
# 用法：
#   bash tools/check-code-size.sh                    检查（默认，CI 用这个）
#   bash tools/check-code-size.sh --update-baseline  重新生成 baseline，只在人类明确要求时用
#
# 规则：
#   1. src/main.cpp 是程序入口，只做装配，上限 300 行。硬规则，不登记 baseline。
#   2. src/**/*.cpp、src/**/*.h（不含 build 目录）单文件硬上限 1500 行。
#      新出现的 >1500 行文件直接判红；已有的历史大文件登记在
#      tools/code-size-baseline.txt，且不得超过登记值（ratchet 只往下走）。
#      文件变小不判红；只有 --update-baseline 会把上限收紧。
#
# 为什么用 ratchet：仓库里仍有一批历史大文件，一次性砍到 1500 会让全仓库变红、
# 逼人绕过门禁。baseline 把“现存债务”冻结成上限，只禁止继续变大；deadline 前
# 逐周用 --update-baseline 收紧，最终删除 baseline。
#
# 退出码：0 通过；1 违反规则或 baseline 格式损坏；2 参数错误。

set -euo pipefail
export LC_ALL=C

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

MAIN=src/main.cpp
MAIN_LIMIT=300
FILE_LIMIT=1500
BASELINE=tools/code-size-baseline.txt
MODE=check

usage() {
    sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case $1 in
        --update-baseline) MODE=update ;;
        -h|--help) usage; exit 0 ;;
        *) echo "错误：未知参数 $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

# 输出「行数<TAB>路径」，按行数降序；排除构建目录与 wc 的 total 行。
collect_sizes() {
    find src -type f \( -name '*.cpp' -o -name '*.h' \) \
        -not -path '*/build/*' -print0 \
        | xargs -0 -r wc -l \
        | awk 'NF >= 2 && $1 ~ /^[0-9]+$/ {
                   n = $1; $1 = ""; sub(/^[ \t]+/, "");
                   if ($0 == "total") next;
                   print n "\t" $0
               }' \
        | sort -t "$(printf '\t')" -k1,1nr -k2,2
}

declare -A BASE=()

# 读 baseline 到 BASE[路径]=行数。missing_ok=1 时文件不存在不算错（用于 --update-baseline）。
load_baseline() {
    local missing_ok=${1:-0} lineno=0 line path limit
    if [[ ! -f $BASELINE ]]; then
        ((missing_ok)) && return 0
        echo "错误：缺少 $BASELINE（运行 --update-baseline 生成）" >&2
        return 1
    fi
    while IFS= read -r line || [[ -n $line ]]; do
        lineno=$((lineno + 1))
        line=${line%$'\r'}
        [[ -z ${line//[[:space:]]/} ]] && continue
        [[ $line == \#* ]] && continue
        if [[ $line != *$'\t'* ]]; then
            echo "错误：$BASELINE 第 $lineno 行应为「路径<TAB>行数」：$line" >&2
            return 1
        fi
        path=${line%%$'\t'*}
        limit=${line#*$'\t'}
        if [[ ! $limit =~ ^[0-9]+$ ]]; then
            echo "错误：$BASELINE 第 $lineno 行行数不是整数：$limit" >&2
            return 1
        fi
        if [[ -n ${BASE[$path]:-} ]]; then
            echo "错误：$BASELINE 重复登记 $path" >&2
            return 1
        fi
        BASE[$path]=$limit
    done < "$BASELINE"
}

check_mode() {
    load_baseline || return 1

    local main_lines rc=0 violations=0 total=0
    main_lines=$(wc -l < "$MAIN" | tr -d ' ')
    : "${main_lines:=0}"

    echo "代码体量检查：$MAIN ≤ $MAIN_LIMIT 行；单文件 > $FILE_LIMIT 行须登记 $BASELINE"
    if ((main_lines > MAIN_LIMIT)); then
        printf '超限：%s 当前 %d 行 > 上限 %d 行（入口只做装配，逻辑放 src/app 等模块）\n' \
            "$MAIN" "$main_lines" "$MAIN_LIMIT"
        rc=1
        violations=$((violations + 1))
    else
        printf 'ok  ：%s %d/%d 行\n' "$MAIN" "$main_lines" "$MAIN_LIMIT"
    fi

    local -a paths=() lines=()
    local p n
    while IFS=$'\t' read -r n p; do
        paths+=("$p")
        lines+=("$n")
    done < <(collect_sizes)
    total=${#paths[@]}

    local -A seen=()
    local i limit
    for ((i = 0; i < total; i++)); do
        p=${paths[i]}
        n=${lines[i]}
        ((n > FILE_LIMIT)) || continue
        seen[$p]=1
        if [[ -n ${BASE[$p]:-} ]]; then
            limit=${BASE[$p]}
            if ((n > limit)); then
                printf '超限：%s 当前 %d 行 > baseline %d 行（阈值 %d 行）；拆文件，不要抬高 baseline\n' \
                    "$p" "$n" "$limit" "$FILE_LIMIT"
                rc=1
                violations=$((violations + 1))
            elif ((n < limit)); then
                printf 'ok  ：%s %d 行，低于 baseline %d 行（可 --update-baseline 收紧）\n' \
                    "$p" "$n" "$limit"
            else
                printf 'ok  ：%s %d 行 = baseline 上限\n' "$p" "$n"
            fi
        else
            printf '新增：%s 当前 %d 行 > 阈值 %d 行，且未登记 baseline\n' \
                "$p" "$n" "$FILE_LIMIT"
            rc=1
            violations=$((violations + 1))
        fi
    done

    # baseline 里已经消失或降到阈值以下的条目：不判红，只提示收紧（ratchet 只降不升）。
    local stale=0
    for p in "${!BASE[@]}"; do
        [[ -n ${seen[$p]:-} ]] && continue
        stale=$((stale + 1))
        if [[ ! -f $p ]]; then
            printf '提示：baseline 条目 %s 已不存在（可 --update-baseline 移除）\n' "$p"
        else
            printf '提示：%s 已降到 %d 行，低于阈值 %d 行（可 --update-baseline 移除）\n' \
                "$p" "$(wc -l < "$p" | tr -d ' ')" "$FILE_LIMIT"
        fi
    done

    printf '汇总：扫描 %d 个源文件，baseline %d 项，可收紧 %d 项，超限 %d 项\n' \
        "$total" "${#BASE[@]}" "$stale" "$violations"
    if ((rc)); then
        echo "失败：拆掉超限文件，或先在 issue/PR 里向人类申请更新 baseline。" >&2
        return 1
    fi
    echo "通过：没有文件变大，也没有新的超大文件。"
}

# 只在人类明确要求时使用：把当前 >FILE_LIMIT 的文件写成新 baseline（同时收紧/移除旧条目）。
update_mode() {
    load_baseline 1 || return 1

    local -a paths=() lines=() keep_paths=() keep_lines=()
    local p n
    while IFS=$'\t' read -r n p; do
        paths+=("$p")
        lines+=("$n")
    done < <(collect_sizes)

    local i
    for i in "${!paths[@]}"; do
        ((lines[i] > FILE_LIMIT)) || continue
        keep_paths+=("${paths[i]}")
        keep_lines+=("${lines[i]}")
        p=${paths[i]}
        n=${lines[i]}
        if [[ -z ${BASE[$p]:-} ]]; then
            printf '新增：%s %d 行\n' "$p" "$n"
        elif ((n > BASE[$p])); then
            printf '抬高：%s %d 行 → %d 行\n' "$p" "${BASE[$p]}" "$n"
        elif ((n < BASE[$p])); then
            printf '收紧：%s %d 行 → %d 行\n' "$p" "${BASE[$p]}" "$n"
        fi
    done
    for p in "${!BASE[@]}"; do
        local found=0
        for i in "${!keep_paths[@]}"; do
            [[ ${keep_paths[i]} == "$p" ]] && { found=1; break; }
        done
        ((found)) || printf '移除：%s（已不超过 %d 行）\n' "$p" "$FILE_LIMIT"
    done

    local tmp
    tmp=$(mktemp) || return 1
    {
        echo "# 代码体量 ratchet baseline：由 tools/check-code-size.sh --update-baseline 生成。"
        echo "#"
        echo "# 只登记当前超过 ${FILE_LIMIT} 行的 .cpp/.h（src/，不含 build）。"
        echo "# 格式：路径<TAB>行数，表示该文件允许的最大行数；超过即 CI 判红。"
        echo "# 文件变小不会自动放宽：需要收紧时由人类明确要求后重新生成。"
        echo "# 不要手工抬高数字来让 CI 变绿——先拆文件。"
        for i in "${!keep_paths[@]}"; do
            printf '%s\t%s\n' "${keep_paths[i]}" "${keep_lines[i]}"
        done
    } > "$tmp"

    if cmp -s "$tmp" "$BASELINE" 2>/dev/null; then
        rm -f "$tmp"
        echo "baseline 无变化（$BASELINE）"
        return 0
    fi
    mv -- "$tmp" "$BASELINE"
    chmod 644 -- "$BASELINE"
    echo "已更新 $BASELINE：${#keep_paths[@]} 项。请单独提交这份 diff。"
}

case $MODE in
    check) check_mode ;;
    update) update_mode ;;
esac

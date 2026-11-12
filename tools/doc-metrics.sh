#!/usr/bin/env bash
# 生成并校验文档里的指标区块（issue #91）。
#
# 指标全部从仓库现场计算，避免文档里的行数/测试数/二进制大小靠手抄而漂移
# （历史上 STATUS.md 写过 268 个自检、9.5MB，早已对不上）。
#
# 用法：
#   bash tools/doc-metrics.sh                 打印 Markdown 片段（默认只读）
#   bash tools/doc-metrics.sh --write         替换 docs/AI/STATUS.md 里的标记区块
#   bash tools/doc-metrics.sh --check         与文档标记区块比对，不一致退出 1
#   bash tools/doc-metrics.sh --build-dir DIR 指定 ctest 用的构建目录
#
# 约定：
#   - 日期 / HEAD 每次都会变，--check 不比这两行。
#   - ctest / 二进制大小 / ldd 依赖需要本地构建产物；算不出来时值写成“未构建”
#     或“缺失”，--check 跳过这些行，--write 保留文档里的旧值，所以干净 checkout
#     也能运行且不会把已有数字改成占位符。
#   - 其余指标（源码行数、自检开关数）必须与文档一致，否则 --check 退出 1。
#
# 退出码：0 同步/成功；1 指标不一致或文档缺少标记区块；2 参数错误。

set -euo pipefail
export LC_ALL=C

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

BEGIN_MARK='<!-- metrics:begin -->'
END_MARK='<!-- metrics:end -->'
DOC='docs/AI/STATUS.md'
BUILD_DIR=${PVFORGE_BUILD_DIR:-}
MODE=print

usage() {
    sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case $1 in
        --write) MODE=write ;;
        --check) MODE=check ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "错误：--build-dir 需要目录参数" >&2; exit 2; }
            BUILD_DIR=$2
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "错误：未知参数 $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

labels=()
values=()
volatile=()

add_row() { # label value [volatile:1]
    labels+=("$1")
    values+=("$2")
    volatile+=("${3:-0}")
}

is_unknown() { # 本轮算不出来的值：--check 跳过，--write 保留旧值
    case $1 in
        未构建*|缺失*|不可用*) return 0 ;;
        *) return 1 ;;
    esac
}

line_count() { # 给 find 的输出统计行数
    wc -l | tr -d ' '
}

pick_build_dir() {
    if [[ -n $BUILD_DIR ]]; then
        printf '%s\n' "$BUILD_DIR"
        return 0
    fi
    local dir
    for dir in build/debug build/tests build; do
        if [[ -f "$dir/CTestTestfile.cmake" ]]; then
            printf '%s\n' "$dir"
            return 0
        fi
    done
    return 1
}

build_rows() {
    labels=()
    values=()
    volatile=()

    local head_sha main_lines tests_dir_lines all_lines test_lines nontest_lines switches

    head_sha=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
    main_lines=$(line_count < src/main.cpp)

    tests_dir_lines=$(find src/tests -type f \( -name '*.cpp' -o -name '*.h' \) \
        -exec cat {} + | line_count)

    # “测试源码”= 任何 tests/ 目录下的源码，加上正文里的 *Test.cpp / *Tests.cpp
    # （如 src/mcp/StdioBridgeTest.cpp）。其余算非测试源码。
    all_lines=$(find src -type f \( -name '*.cpp' -o -name '*.h' \) \
        -exec cat {} + | line_count)
    test_lines=$(find src -type f \( -name '*.cpp' -o -name '*.h' \) \
        \( -path '*/tests/*' -o -name '*Test.cpp' -o -name '*Tests.cpp' \) \
        -exec cat {} + | line_count)
    nontest_lines=$((all_lines - test_lines))

    # --*-test 开关的唯一来源是 registry.add(QStringLiteral("--...-test"))。
    switches=$({ grep -rhoE --include='*.cpp' --include='*.h' \
        'registry\.add\(QStringLiteral\("--[a-z0-9-]+-test"\)' src || true; } \
        | sort -u | line_count)

    local ctest_value='未构建（无 build 目录，校验时跳过）'
    local build_dir
    if build_dir=$(pick_build_dir); then
        if command -v ctest >/dev/null 2>&1; then
            local count
            count=$(ctest --test-dir "$build_dir" -N 2>/dev/null \
                | sed -n 's/^Total Tests: *//p' | tail -1 || true)
            if [[ -n $count ]]; then
                ctest_value=$count
            else
                ctest_value='不可用（该目录没有 CTest 用例）'
            fi
        else
            ctest_value='不可用（未安装 ctest）'
        fi
    fi

    local size_value='缺失（未构建 dist/pvforge，校验时跳过）'
    local deps_value='缺失（未构建 dist/pvforge，校验时跳过）'
    if [[ -f dist/pvforge ]]; then
        local bytes
        bytes=$(stat -c %s dist/pvforge 2>/dev/null || stat -f %z dist/pvforge)
        if [[ -n ${bytes:-} ]]; then
            size_value=$(awk -v b="$bytes" 'BEGIN { printf "%.1f MiB（%s 字节）", b / 1048576, b }')
        else
            size_value='不可用（stat 无法读取大小）'
        fi
        if command -v ldd >/dev/null 2>&1; then
            local deps
            deps=$(ldd dist/pvforge 2>/dev/null | awk '/=>/ { n++ } END { print n + 0 }')
            deps_value="$deps（ldd 动态库依赖）"
        else
            deps_value='不可用（无 ldd）'
        fi
    fi

    add_row '生成日期' "$(date +%F)" 1
    add_row 'HEAD' "\`$head_sha\`" 1
    add_row '`src/main.cpp` 行数' "$main_lines"
    add_row '`src/tests` 行数' "$tests_dir_lines"
    add_row '测试源码行数（含模块 tests）' "$test_lines"
    add_row '非测试源码行数' "$nontest_lines"
    add_row '`--*-test` 自检开关数' "$switches"
    add_row 'CTest 用例数' "$ctest_value"
    add_row '`dist/pvforge` 大小' "$size_value"
    add_row 'Release 动态库依赖数' "$deps_value"
}

render_block() {
    printf '%s\n' "$BEGIN_MARK"
    printf '%s\n' '<!-- 本区块由 `bash tools/doc-metrics.sh --write` 生成，请勿手改；CI 用 `--check` 校验。 -->'
    printf '%s\n' '| 指标 | 值 |'
    printf '%s\n' '| --- | --- |'
    local i
    for ((i = 0; i < ${#labels[@]}; i++)); do
        printf '| %s | %s |\n' "${labels[i]}" "${values[i]}"
    done
    printf '%s\n' "$END_MARK"
}

extract_block() {
    awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
        $0 == b { inside = 1; next }
        $0 == e { inside = 0; next }
        inside  { print }
    ' "$DOC"
}

# 把文档区块里的表格行读进 doc_labels/doc_values。
load_doc_rows() {
    doc_labels=()
    doc_values=()
    [[ -f $DOC ]] || return 1
    if [[ $(grep -cF -- "$BEGIN_MARK" "$DOC" || true) -ne 1 ]] \
        || [[ $(grep -cF -- "$END_MARK" "$DOC" || true) -ne 1 ]]; then
        return 1
    fi
    local block
    block=$(extract_block)
    [[ -n $block ]] || return 1
    local label value
    while IFS=$'\t' read -r label value; do
        doc_labels+=("$label")
        doc_values+=("$value")
    done < <(printf '%s\n' "$block" | awk -F'|' '
        /^[[:space:]]*\|/ {
            label = $2; value = $3
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", label)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            if (label == "指标" || label ~ /^-+$/) next
            print label "\t" value
        }')
    ((${#doc_labels[@]}))
}

check_mode() {
    if ! load_doc_rows; then
        echo "错误：$DOC 里找不到唯一的指标标记区块（$BEGIN_MARK / $END_MARK）" >&2
        echo "先手动放入空区块，再运行：bash tools/doc-metrics.sh --write" >&2
        return 1
    fi
    build_rows

    # 按标签匹配而不是按位置：多一行/少一行不会产生一整串错位报告。
    local bad=0 found i j
    for ((i = 0; i < ${#labels[@]}; i++)); do
        found=0
        for ((j = 0; j < ${#doc_labels[@]}; j++)); do
            [[ ${doc_labels[j]} == "${labels[i]}" ]] || continue
            found=1
            ((volatile[i])) && break
            is_unknown "${values[i]}" && break
            if [[ ${doc_values[j]} != "${values[i]}" ]]; then
                echo "指标漂移：${labels[i]}：文档「${doc_values[j]}」≠ 实际「${values[i]}」" >&2
                bad=1
            fi
            break
        done
        if ((found == 0)); then
            echo "错误：文档指标区块缺少行「${labels[i]}」" >&2
            bad=1
        fi
    done

    # 文档里多出来的行（通常是旧指标或手写残留）。
    for ((j = 0; j < ${#doc_labels[@]}; j++)); do
        found=0
        for ((i = 0; i < ${#labels[@]}; i++)); do
            [[ ${doc_labels[j]} == "${labels[i]}" ]] && { found=1; break; }
        done
        if ((found == 0)); then
            echo "错误：文档指标区块有未知行「${doc_labels[j]}」" >&2
            bad=1
        fi
    done

    if ((bad)); then
        echo "运行 bash tools/doc-metrics.sh --write 更新 $DOC 的指标区块。" >&2
        return 1
    fi
    echo "指标区块已同步（$DOC）"
}

write_mode() {
    if [[ ! -f $DOC ]] \
        || [[ $(grep -cF -- "$BEGIN_MARK" "$DOC" || true) -ne 1 ]] \
        || [[ $(grep -cF -- "$END_MARK" "$DOC" || true) -ne 1 ]]; then
        echo "错误：$DOC 里找不到唯一的指标标记区块（$BEGIN_MARK / $END_MARK）" >&2
        echo "先手动放入空区块，再运行：bash tools/doc-metrics.sh --write" >&2
        return 1
    fi
    build_rows

    # 算不出来的指标（无 build 目录 / 无 dist）保留文档里的旧值，避免干净
    # checkout 上 --write 把真实数字覆盖成“未构建”。
    local preserved=0
    if load_doc_rows; then preserved=1; fi
    local i j
    for ((i = 0; i < ${#labels[@]}; i++)); do
        is_unknown "${values[i]}" || continue
        ((preserved)) || continue
        for ((j = 0; j < ${#doc_labels[@]}; j++)); do
            [[ ${doc_labels[j]} == "${labels[i]}" ]] || continue
            is_unknown "${doc_values[j]}" && break
            values[i]=${doc_values[j]}
            break
        done
    done

    local blockf tmp
    blockf=$(mktemp) || return 1
    tmp=$(mktemp "${DOC}.XXXXXX") || { rm -f "$blockf"; return 1; }
    render_block > "$blockf"
    if ! awk -v b="$BEGIN_MARK" -v e="$END_MARK" -v nf="$blockf" '
        $0 == b { while ((getline line < nf) > 0) print line; close(nf); skipping = 1; next }
        $0 == e { skipping = 0; next }
        !skipping { print }
    ' "$DOC" > "$tmp"; then
        rm -f "$blockf" "$tmp"
        return 1
    fi
    mv -- "$tmp" "$DOC"
    rm -f "$blockf"
    echo "已更新 $DOC 的指标区块"
}

case $MODE in
    print)
        build_rows
        render_block
        for i in "${!values[@]}"; do
            if is_unknown "${values[i]}"; then
                echo "提示：${labels[i]} 本轮无法计算（无 build 目录或 dist 产物），已标注；--write 会保留文档旧值。" >&2
            fi
        done
        ;;
    write) write_mode ;;
    check) check_mode ;;
esac

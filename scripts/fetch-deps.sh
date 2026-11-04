#!/usr/bin/env bash
# fetch-deps.sh — Linux 侧第三方依赖获取（WP-1.2a + FR-4）：ORT 官方 tgz + GTCRN/UVR 两模型
#   + DFN3 目录模型整包（FR-4 质量档，tar.gz 解包至 models/denoiser/dfn3/）+ parity 参考
#   deep-filter CLI 预编译二进制（third_party/tools/，PAR-DF 金样本生成用）
# Qt/FFmpeg/SoundTouch 走 apt（README 技术栈节），不归本脚本管。
# 用法：
#   ./fetch-deps.sh           正常获取：已在位且哈希符合的跳过，缺失的下载
#   ./fetch-deps.sh --check   零副作用自检：只校验/报告，不下载不改动
# 哈希矩阵与 docs/spec/ci-build-strategy.md §4 一一对应（唯一事实源）
set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

ORT_URL=https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-linux-x64-1.30.0.tgz
ORT_SHA=a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd
GTCRN_URL=https://github.com/k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/gtcrn_simple.onnx
GTCRN_SHA=e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534
UVR_URL=https://hf-mirror.com/Politrees/UVR_resources/resolve/main/models/MDXNet/UVR-MDX-NET-Voc_FT.onnx
UVR_SHA=534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111
# FR-4 质量档（DFN3）：模型整包 tar.gz + parity 参考实现（官方 deep-filter CLI v0.5.1 musl 预编译，
# NFR-Q1「DFN3↔官方 Rust runtime」以此闭合；spec 附裁 B 记 cargo 构建主链，实现轮校准回 Releases
# 预编译——网络恢复后核实资产在位且哈希与本地一致，见 fr4 round-1.md §附裁 B 落地校准）
DFN3_URL=https://raw.githubusercontent.com/Rikorose/DeepFilterNet/main/models/DeepFilterNet3_onnx.tar.gz
DFN3_SHA=c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616
DFCLI_URL=https://github.com/Rikorose/DeepFilterNet/releases/download/v0.5.1/deep-filter-0.5.1-x86_64-unknown-linux-musl
DFCLI_SHA=ea9c8ce420bc7dfbba7deb5a3a980903f8ef31b9bde7647ea5458e8a767659bc

missing=0

# ---- 单文件：哈希符合即在位，缺失才下载 ----
ensure_file() { # ensure_file <url> <sha256> <dest>
    local url=$1 want=$2 dest=$3
    if [ -f "$dest" ]; then
        if echo "$want  $dest" | sha256sum -c --quiet 2>/dev/null; then
            echo "  OK     $dest"
        else
            echo "  哈希不符(未动): $dest"; missing=1
        fi
        return
    fi
    if [ $CHECK -eq 1 ]; then echo "  缺失   $dest"; missing=1; return; fi
    mkdir -p "$(dirname "$dest")"
    curl -fL --retry 3 -o "$dest.part" "$url"
    echo "$want  $dest.part" | sha256sum -c --quiet
    mv "$dest.part" "$dest"
    echo "  下载   $dest"
}

# ---- ORT tgz：解包出 include+lib 到 third_party/ort/linux（.sha256 戳记版本）----
ensure_ort() {
    local dir=third_party/ort/linux
    if [ -f "$dir/lib/libonnxruntime.so" ] && [ "$(cat "$dir/.sha256" 2>/dev/null)" = "$ORT_SHA" ]; then
        echo "  OK     $dir（ORT 1.30.0）"; return
    fi
    if [ $CHECK -eq 1 ]; then echo "  缺失   $dir（ORT 1.30.0）"; missing=1; return; fi
    local tmp
    tmp=$(mktemp -d)
    curl -fL --retry 3 -o "$tmp/ort.tgz" "$ORT_URL"
    echo "$ORT_SHA  $tmp/ort.tgz" | sha256sum -c --quiet
    tar xzf "$tmp/ort.tgz" -C "$tmp" --strip-components=1
    rm -rf "$dir" "$dir.new"
    mkdir -p "$dir.new"
    mv "$tmp/include" "$tmp/lib" "$dir.new/"
    mv "$dir.new" "$dir"
    echo "$ORT_SHA" > "$dir/.sha256"
    rm -rf "$tmp"
    echo "  下载   $dir（ORT 1.30.0）"
}

# ---- DFN3 目录模型：tar.gz 下载校验后解包出 4 文件到 third_party/models/denoiser/dfn3 ----
#（官方 tar 内文件在多层单子目录前缀下；模型形态=目录，config.ini+三 onnx 关键文件集在位即信。
#   tar 原包保留在 third_party/models/denoiser/：deep-filter CLI 金样本生成（-m 需 tar）与
#   FR4-MDL 导入用例共用，免二次下载）
ensure_dfn3() {
    local dir=third_party/models/denoiser/dfn3
    local tar=third_party/models/denoiser/DeepFilterNet3_onnx.tar.gz
    local ok=1
    for f in config.ini enc.onnx erb_dec.onnx df_dec.onnx; do
        [ -f "$dir/$f" ] || ok=0
    done
    if [ $ok -eq 1 ] && [ "$(cat "$dir/.sha256" 2>/dev/null)" = "$DFN3_SHA" ] && [ -f "$tar" ]; then
        echo "  OK     $dir（DFN3 目录模型 + 官方 tar 原包）"; return
    fi
    if [ $CHECK -eq 1 ]; then echo "  缺失   $dir（DFN3 目录模型）"; missing=1; return; fi
    local tmp
    tmp=$(mktemp -d)
    curl -fL --retry 3 -o "$tmp/dfn3.tar.gz" "$DFN3_URL"
    echo "$DFN3_SHA  $tmp/dfn3.tar.gz" | sha256sum -c --quiet
    mkdir -p "$tmp/x" && tar xzf "$tmp/dfn3.tar.gz" -C "$tmp/x"
    # 下钻多层单子目录前缀（tmp/export/…）到 config.ini 所在层（空目录即断，防路径自叠死循环）
    local inner="$tmp/x"
    while [ ! -f "$inner/config.ini" ]; do
        local sub
        sub=$(ls -A "$inner") || true
        [ -n "$sub" ] || { echo "fetch-deps: dfn3 解包异常（$inner 为空，无 config.ini）" >&2; exit 1; }
        inner="$inner/$sub"
    done
    rm -rf "$dir" "$dir.new" "$tar"
    mkdir -p "$dir.new" third_party/models/denoiser
    mv "$inner"/config.ini "$inner"/enc.onnx "$inner"/erb_dec.onnx "$inner"/df_dec.onnx "$dir.new/"
    mv "$dir.new" "$dir"
    mv "$tmp/dfn3.tar.gz" "$tar"
    echo "$DFN3_SHA" > "$dir/.sha256"
    rm -rf "$tmp"
    echo "  下载   $dir（DFN3 目录模型 + 官方 tar 原包）"
}

echo "== fetch-deps（Linux）=="
ensure_ort
ensure_file "$GTCRN_URL" "$GTCRN_SHA" third_party/models/denoiser/gtcrn_simple.onnx
ensure_file "$UVR_URL"   "$UVR_SHA"   third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
ensure_dfn3
ensure_file "$DFCLI_URL" "$DFCLI_SHA" third_party/tools/deep-filter && chmod +x third_party/tools/deep-filter
if [ $missing -eq 0 ]; then echo "== 全部就位 =="; else echo "== 存在缺失/不符 =="; exit 1; fi

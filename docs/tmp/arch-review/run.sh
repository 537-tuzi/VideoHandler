#!/usr/bin/env bash
# run.sh — 编译并运行 fft_probe（架构审视测试取证，2026-10-14）
# 用法：./run.sh   （需 g++；结果 tee 到 results.txt）
set -euo pipefail
cd "$(dirname "$0")"
repo="$(cd ../../.. && pwd)"
: "${CXX:=g++}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -I"$repo/src" fft_probe.cpp -o fft_probe
./fft_probe | tee results.txt
echo "完成：$(wc -l < results.txt) 行结果已落 $(pwd)/results.txt"

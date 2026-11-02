#!/usr/bin/env bash
# fake-engine-e2e.sh — NFR-M1 金标准验收（REG 族，spec §6）：临时 detached worktree 注入假引擎
# gain（样本乘 10^(gainDb/20)），实证「新增引擎 = 新 .h/.cpp + 注册表追加条目 + CMake 两行，
# 命令层/GUI/服务层零改」。断言：REG-001 改动面白名单 / REG-002 正例 e2e（result 全字段 +
# RMS 比值 ≈ 10^(6/20)）/ REG-003..007 负例（消息模板与真引擎同构）/ REG-008 未注册动词拒收 /
# REG-009..011 String 三态负例（Kind::String 分支唯一 e2e 覆盖）。
# 方法论（OQ-1 拍板）：worktree 内先落「片 A 基线」临时 detached 提交（不入 main，用后丢弃），
# 假引擎以未提交形态注入——git status 即白名单证据，main 历史零痕迹。
# 依赖：git / cmake+Ninja（linux-test preset 同套，pkg-config Qt6Core/FFmpeg/SoundTouch）/
#       ffmpeg / python3（stdlib json+struct）；third_party 经软链共享（ORT+模型）
# 用法：fake-engine-e2e.sh
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→nfr-m1-engine-registry→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }
REPO=$(pwd)
# json_check.py 断言器复用 fr24 的（交叉引用，不复制）
S=docs/acceptance/fr24-command-layer/scripts

W=$(mktemp -d /tmp/nfr-m1-reg.XXXXXX)
WT="$W/wt"
cleaned=0
cleanup() { [ "$cleaned" = 1 ] && return; cleaned=1; git -C "$REPO" worktree remove --force "$WT" >/dev/null 2>&1; rm -rf "$W"; }
trap cleanup EXIT

FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

echo "== worktree（detached @ HEAD + 片 A 基线临时提交，third_party 软链共享）=="
git -C "$REPO" worktree add --detach "$WT" HEAD >/dev/null || { echo "FAIL: worktree add"; exit 1; }
ln -s "$REPO/third_party" "$WT/third_party"
# 片 A 四文件拷入并做 detached 提交：REG-001 的 diff 基线 =「片 A 已就位」，
# 假引擎注入的改动面才可与金标准白名单比对；detached 提交不触 main（无分支引用，用后即弃）
cp src/service/engineregistry.h src/service/engineregistry.cpp src/service/commandlayer.cpp "$WT/src/service/"
cp CMakeLists.txt "$WT/CMakeLists.txt"
git -C "$WT" add -A
git -C "$WT" -c user.name=nfr-m1 -c user.email=nfr-m1@local diff --cached --quiet \
    || git -C "$WT" -c user.name=nfr-m1 -c user.email=nfr-m1@local commit -q -m "NFR-M1 片 A 基线（临时 detached 提交，验收用后即弃）"

# —— 假引擎（~40 行：IStreamEngine 最小形态，参考 dspdenoiser 直通风格）——
cat > "$WT/src/service/gainengine.h" <<'EOF'
#pragma once

// 假引擎（NFR-M1 REG 族验收专用，不入 main）：样本乘 10^(gainDb/20)，跟随源、无内部缓冲
#include "streamengine.h"

class GainEngine : public IStreamEngine
{
public:
    void setup(int sampleRate, int channels, double gainDb);
    int inRate() const override { return rate_; }
    int inChannels() const override { return channels_; }
    std::vector<float> process(const float* in, int64_t frames) override;
    std::vector<float> flush() override { return {}; }
private:
    int rate_ = 0, channels_ = 0;
    float gain_ = 1.0f;
};
EOF
cat > "$WT/src/service/gainengine.cpp" <<'EOF'
#include "gainengine.h"

#include <cmath>

void GainEngine::setup(int sampleRate, int channels, double gainDb) {
    rate_ = sampleRate;
    channels_ = channels;
    gain_ = static_cast<float>(std::pow(10.0, gainDb / 20.0));
}

std::vector<float> GainEngine::process(const float* in, int64_t frames) {
    std::vector<float> out(in, in + frames * static_cast<int64_t>(channels_));
    for (float& s : out) s *= gain_;
    return out;
}
EOF

# —— 注册表追加条目 + CMake 两行（python 锚点替换，锚唯一性逐一断言）——
python3 - "$WT" <<'PYEOF' || exit 1
import sys
wt = sys.argv[1]

p = wt + "/src/service/engineregistry.cpp"
src = open(p, encoding="utf-8").read()

a1 = '#include "vocalseparator.h"\n'
assert src.count(a1) == 1, "锚1 不唯一：vocalseparator include"
src = src.replace(a1, a1 + '#include "gainengine.h"\n', 1)

a2 = "}// namespace\n\nconst QList<EngineEntry>& engineTable() {"
assert src.count(a2) == 1, "锚2 不唯一：匿名 ns 结束"
gain_ns = '''// ---- NFR-M1 验收假引擎（REG 族注入，不入 main）----

std::unique_ptr<IStreamEngine> createGain(const QJsonObject& args, int srcRate, int srcCh, QString*) {
    auto engine = std::make_unique<GainEngine>();
    engine->setup(srcRate, srcCh, args.value(QLatin1String("gainDb")).toDouble());
    return engine;
}

QString suffixGain(const QJsonObject&) { return QStringLiteral("_gain"); }

const ParamSpec kGainDb = {// gainDb 数字必填 [-24,24] 且非 0（覆盖 Number 全部校验分支）
    .name = "gainDb",
    .kind = ParamSpec::Kind::Number,
    .required = true,
    .min = -24.0,
    .max = 24.0,
    .hasRange = true,
    .rejectZero = true,
    .values = {},
    .defStr = nullptr,
};

const ParamSpec kNote = {// note 字符串必填（覆盖 String 全部校验分支：缺失/类型错/空串——五真条目无 String 参数，唯此处实证）
    .name = "note",
    .kind = ParamSpec::Kind::String,
    .required = true,
    .min = 0,
    .max = 0,
    .hasRange = false,
    .rejectZero = false,
    .values = {},
    .defStr = nullptr,
};

'''
src = src.replace(a2, gain_ns + a2, 1)

a3 = "            .create = createSeparate,\n        },\n    };"
assert src.count(a3) == 1, "锚3 不唯一：表尾 separate 条目"
entry = '''            .create = createSeparate,
        },
        EngineEntry{// NFR-M1 验收假引擎（REG 族注入，不入 main）
            .id = QStringLiteral("gain"),
            .params = {kGainDb, kNote},
            .requiresModel = false,
            .setupErrorClass = nullptr,
            .lockCanonicalOutput = false,
            .outputSuffix = suffixGain,
            .semanticCheck = nullptr,
            .create = createGain,
        },
    };'''
src = src.replace(a3, entry, 1)
open(p, "w", encoding="utf-8").write(src)

p = wt + "/CMakeLists.txt"
src = open(p, encoding="utf-8").read()
a4 = "    src/service/fftranscoder.cpp\n"
assert src.count(a4) == 1, "锚4 不唯一：CMake fftranscoder 行"
src = src.replace(a4, a4 + "    src/service/gainengine.h\n    src/service/gainengine.cpp\n", 1)
open(p, "w", encoding="utf-8").write(src)
print("注入完成：include + 闭包/spec + 表条目 + CMake 两行")
PYEOF

echo "== 构建（linux-test，零警告）=="
( cd "$WT" && cmake --preset linux-test >/dev/null && cmake --build --preset linux-test >/dev/null ) 2>"$W/build.err" \
    || { echo "FAIL: worktree 构建"; cat "$W/build.err"; exit 1; }
if grep -iE "warn|error" "$W/build.err" | grep -v "^--" >/dev/null; then
    echo "FAIL: 构建有告警"; grep -iE "warn|error" "$W/build.err" | head; exit 1
fi
echo "  PASS 构建零警告"
VH="$WT/build-linux/video_handler"

echo "== REG-001 改动面白名单（git status 恰四处：gainengine 新增×2 + 注册表/CMake 修改×2）=="
got=$(git -C "$WT" status --porcelain | sort)
want=$(printf '%s\n' ' M CMakeLists.txt' ' M src/service/engineregistry.cpp' '?? src/service/gainengine.cpp' '?? src/service/gainengine.h')
[ "$got" = "$want" ]; ck $? "REG-001（commandlayer/ffextractor/mediaservice/mainwidget/main/streamengine 零改动）"

echo "== 素材（5s 44.1k 立体声正弦，run-json 同配方）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/in.wav"

# jr <id> <期望退出码> <请求JSON> [断言...]（json_check.py 断言器，run-json 同款）
jr() {
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>/dev/null
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$S/json_check.py" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"; else FAIL=1; fi
}

echo "== REG-002 假动词 e2e 正例（outputSpec 跟随源 + engineResampled=false + result 全字段）=="
jr REG-002 0 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/out.wav\",\"gainDb\":6,\"note\":\"reg\"}" \
    ok=true v=1 cmd=gain num:result.sourceSpec.sampleRate=44100 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    str:result.outputSpec.format=pcm_f32le bool:result.engineResampled=false \
    num:result.frames=220500 num:result.durationSec=5 exists:result.output

echo "== REG-002r 输出/输入 RMS 比值 ≈ 10^(6/20)（python3 stdlib struct 读 f32/s16，容差 1%）=="
python3 - "$W/in.wav" "$W/out.wav" <<'PYEOF'
import struct, sys

def read_wav(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", path
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        cid, sz = d[pos:pos+4], struct.unpack("<I", d[pos+4:pos+8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", d[pos+8:pos+8+16])
        elif cid == b"data":
            data = d[pos+8:pos+8+sz]
        pos += 8 + sz + (sz & 1)
    return fmt, data

f1, d1 = read_wav(sys.argv[1])
f2, d2 = read_wav(sys.argv[2])
assert f1[0] == 1, "输入应为 s16 PCM"
assert f2[0] == 3, "输出应为 IEEE float（pcm_f32le）"
n1 = struct.unpack("<%dh" % (len(d1) // 2), d1)
n2 = struct.unpack("<%df" % (len(d2) // 4), d2)

def rms(xs):
    return (sum(float(x) * float(x) for x in xs) / len(xs)) ** 0.5

ratio = rms(n2) / (rms(n1) / 32768.0)
expect = 10.0 ** (6.0 / 20.0)
print(f"  RMS 比值 {ratio:.6f} vs 期望 {expect:.6f}（偏差 {abs(ratio-expect)/expect*100:.4f}%）")
sys.exit(0 if abs(ratio - expect) <= 0.01 * expect else 1)
PYEOF
ck $? "REG-002r（RMS 比值）"

echo "== REG-003..007 负例（E_PARAMS exit 1 无 result；消息模板与真引擎同构；note 恒合法以聚焦 gainDb）=="
jr REG-003 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"note\":\"reg\"}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=gainDb absent:result
jr REG-004 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":\"abc\",\"note\":\"reg\"}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=必须是数字 absent:result
jr REG-005 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":30,\"note\":\"reg\"}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=须在 absent:result
jr REG-006 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":0,\"note\":\"reg\"}" \
    ok=false cmd=gain code=E_PARAMS "msg-contains=不能为 0" absent:result
jr REG-007 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":6,\"note\":\"reg\",\"bogus\":1}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=未知字段 absent:result
ex_nego() {
    local id=$1 frag=$2
    python3 -c "import json,sys; d=json.load(open('$W/$id.resp')); sys.exit(0 if '$frag' in d['error']['message'] else 1)"
    ck $? "$id 文案含「$frag」（模板逐字同构）"
}
ex_nego REG-003 "gainDb 必须是数字"
ex_nego REG-005 "gainDb 须在 [-24, 24] 内"
ex_nego REG-006 "gainDb 不能为 0"
ex_nego REG-007 "未知字段：bogus"

echo "== REG-009..011 String 三态负例（Kind::String 分支唯一 e2e：缺失/类型错/空串；审查 P2-2 补强）=="
jr REG-009 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":6}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=note absent:result
jr REG-010 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":6,\"note\":123}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=note absent:result
jr REG-011 1 "{\"v\":1,\"cmd\":\"gain\",\"input\":\"$W/in.wav\",\"output\":\"$W/x.wav\",\"gainDb\":6,\"note\":\"\"}" \
    ok=false cmd=gain code=E_PARAMS msg-contains=note absent:result
ex_nego REG-009 "note 缺失"
ex_nego REG-010 "note 必须是字符串"
ex_nego REG-011 "note 不能为空"

echo "== REG-008 未注册动词 → E_PARAMS「未知命令」且响应不回显 cmd =="
jr REG-008 1 '{"v":1,"cmd":"frobnicate","input":"x"}' ok=false no-cmd code=E_PARAMS msg-contains=未知命令 absent:result

echo "== worktree 清理（git worktree list 复核无残留）=="
cleanup
git -C "$REPO" worktree list
git -C "$REPO" worktree list | grep -q "$WT" && { echo "FAIL: worktree 残留"; exit 1; }
echo "  PASS worktree 已清（main 历史零痕迹，detached 提交无引用即弃）"

if [ "$FAIL" = 1 ]; then echo "== fake-engine-e2e FAIL（见上）=="; exit 1; fi
echo "== fake-engine-e2e 全绿：新增引擎只触三处文件（假引擎+条目+CMake），命令层/GUI/服务层零改 =="

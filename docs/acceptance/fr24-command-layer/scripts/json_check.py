#!/usr/bin/env python3
# json_check.py — FR-24 命令层响应断言器（run-json.sh 用；stdlib json，不引 jq——CI 环境不保证）
# 用法：json_check.py <响应文件> [断言...]，全部通过退出 0，任一失败打印 FAIL 行退出 1
# 断言语法（path 支持 . 分隔与数组下标，如 result.streams.0.type）：
#   ok=true|false        顶层 ok
#   v=1                  顶层 v
#   cmd=denoise          顶层 cmd 回显
#   no-cmd               顶层不得有 cmd（cmd 能识别才回显）
#   code=E_MODEL         error.code
#   msg-contains=片段    error.message 含片段
#   num:<path>=<值>      数字相等
#   str:<path>=<值>      字符串相等
#   bool:<path>=true     布尔相等
#   contains:<path>=<片段> 字符串包含
#   numrange:<path>=<min>:<max> 数字落在闭区间
#   absent:<path>        路径不得存在（负例防 result 泄漏）
#   exists:<path>        路径取值为字符串且指向的文件存在
import json
import os
import sys


def get(doc, path):
    cur = doc
    for part in path.split("."):
        if isinstance(cur, list):
            if not part.isdigit() or int(part) >= len(cur):
                return None, False
            cur = cur[int(part)]
        elif isinstance(cur, dict) and part in cur:
            cur = cur[part]
        else:
            return None, False
    return cur, True


def main():
    try:
        with open(sys.argv[1], "rb") as f:
            doc = json.load(f)
    except Exception as e:  # noqa: BLE001
        print(f"FAIL: 响应不是合法 JSON：{e}")
        return 1
    if not isinstance(doc, dict):
        print("FAIL: 响应不是 JSON 对象")
        return 1
    bad = []
    for a in sys.argv[2:]:
        if a == "no-cmd":
            if "cmd" in doc:
                bad.append(f"cmd 应缺失，实为 {doc['cmd']!r}")
        elif a.startswith("msg-contains="):
            v, ok = get(doc, "error.message")
            if not ok or a.split("=", 1)[1] not in str(v):
                bad.append(f"error.message 不含期望片段：{v!r}")
        elif a.startswith("absent:"):
            v, ok = get(doc, a.split(":", 1)[1])
            if ok:
                bad.append(f"{a.split(':', 1)[1]} 应缺失，实为 {v!r}")
        elif a.startswith("exists:"):
            v, ok = get(doc, a.split(":", 1)[1])
            if not ok or not isinstance(v, str) or not os.path.exists(v):
                bad.append(f"路径不存在：{v!r}")
        elif "=" in a and ":" in a.split("=", 1)[0]:
            head, rest = a.split(":", 1)
            path, want = rest.split("=", 1)
            v, ok = get(doc, path)
            if head == "num":
                if not ok or not isinstance(v, (int, float)) or float(v) != float(want):
                    bad.append(f"{path} 期望数字 {want}，实为 {v!r}")
            elif head == "str":
                if not ok or v != want:
                    bad.append(f"{path} 期望 {want!r}，实为 {v!r}")
            elif head == "bool":
                if not ok or not isinstance(v, bool) or v is (want != "true"):# isinstance 前置：数字 0/1 不得冒充 bool（审查 P2-2）
                    bad.append(f"{path} 期望 {want}，实为 {v!r}")
            elif head == "contains":
                if not ok or want not in str(v):
                    bad.append(f"{path} 期望包含 {want!r}，实为 {v!r}")
            elif head == "numrange":
                lo, hi = want.split(":", 1)
                if not ok or not isinstance(v, (int, float)) or not (float(lo) <= float(v) <= float(hi)):
                    bad.append(f"{path} 期望落在 [{lo},{hi}]，实为 {v!r}")
            else:
                bad.append(f"未知断言：{a}")
        elif "=" in a:
            key, want = a.split("=", 1)
            if key == "code":
                v, ok = get(doc, "error.code")
                if not ok or v != want:
                    bad.append(f"error.code 期望 {want!r}，实为 {v!r}")
            elif key in ("ok", "v", "cmd"):
                v, ok = get(doc, key)
                if key == "ok":
                    if not isinstance(v, bool) or v is not (want == "true"):# 同 P2-2：类型前置
                        bad.append(f"ok 期望 {want}，实为 {v!r}")
                elif key == "v":
                    if not ok or float(v) != float(want):
                        bad.append(f"v 期望 {want}，实为 {v!r}")
                else:
                    if not ok or v != want:
                        bad.append(f"cmd 期望 {want!r}，实为 {v!r}")
            else:
                bad.append(f"未知断言：{a}")
        else:
            bad.append(f"未知断言：{a}")
    if bad:
        for b in bad:
            print(f"FAIL: {b}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

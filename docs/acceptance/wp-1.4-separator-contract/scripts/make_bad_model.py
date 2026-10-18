#!/usr/bin/env python3
"""WP-1.4 负路径验收工具：构造输出违约的假 MDX 模型 + 临时注入参数表。

子命令：
  make <src.onnx> <outdir>   产出 bad-layout.onnx（输出 Transpose 成 [1,4,dimT,dimF]）
                              与 bad-dtype.onnx（输出 Cast 成 double），打印各自末 10MiB MD5
  inject <cpp文件> <md5>     往 kMdxModelData 表临时注入 <md5> 条目（参数拷贝 Voc_FT），测完须 restore
  restore <cpp文件>          还原注入（按 BEGIN/END 标记删除）
  --check                    零副作用自检：参数齐全、文件存在/可解析

用法示例（WP-1.4 round-1 负路径）：
  make_bad_model.py --check make UVR-MDX-NET-Voc_FT.onnx /tmp/wp14
  make_bad_model.py make ... && engine_test sep-voc ... bad-layout.onnx   # 未注入 → 「不认识的模型」
  make_bad_model.py inject src/service/vocalseparator.cpp <md5> && 重编译   # 注入 → 「输出布局不符」
  make_bad_model.py restore src/service/vocalseparator.cpp
"""
import hashlib
import sys
from pathlib import Path

VOC_FT_PARAMS = '"compensate":1.021,"mdx_dim_f_set":3072,"mdx_dim_t_set":8,"mdx_n_fft_scale_set":7680,"primary_stem":"Vocals"'
# 注入标记必须是合法 JSON（冗余键值对）：C 风格注释会让 fromJson 整表解析失败
INJECT_BEGIN = '"zz-badmodel-begin":"wp14",'
INJECT_END = '"zz-badmodel-end":"wp14",'


def tail_md5(path: Path) -> str:
    """与 C++ tailMd5 同算法：文件末 10000 KiB（audio-separator 表生成器实取 -10000*1024，非 10MiB）。"""
    size = path.stat().st_size
    start = max(0, size - 10000 * 1024)
    h = hashlib.md5()
    with open(path, 'rb') as f:
        f.seek(start)
        while True:
            b = f.read(65536)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def do_make(src: Path, outdir: Path):
    import onnx
    from onnx import helper, TensorProto
    m = onnx.load(str(src))
    g = m.graph
    out_name = g.output[0].name
    src_node = next(n for n in g.node if out_name in n.output)
    src_node.output[list(src_node.output).index(out_name)] = out_name + '_src'

    def save_variant(node, out_elem, out_dims, dst: Path):
        g.node.append(node)
        vi = g.output[0]
        vi.type.tensor_type.elem_type = out_elem
        for i, d in enumerate(out_dims):
            del vi.type.tensor_type.shape.dim[:]
            for d2 in out_dims:
                vi.type.tensor_type.shape.dim.add().dim_value = d2
            break
        onnx.checker.check_model(m)
        onnx.save(m, str(dst))
        g.node.pop()

    d1 = [d.dim_value for d in g.output[0].type.tensor_type.shape.dim]
    save_variant(helper.make_node('Transpose', [out_name + '_src'], [out_name], perm=[0, 1, 3, 2]),
                 TensorProto.FLOAT, [d1[0], d1[1], d1[3], d1[2]], outdir / 'bad-layout.onnx')
    save_variant(helper.make_node('Cast', [out_name + '_src'], [out_name], to=TensorProto.DOUBLE),
                 TensorProto.DOUBLE, d1, outdir / 'bad-dtype.onnx')
    for n in ('bad-layout.onnx', 'bad-dtype.onnx'):
        p = outdir / n
        print(f'{n}: {p}  末10MiB MD5={tail_md5(p)}')


def do_inject(cpp: Path, md5s):
    text = cpp.read_text(encoding='utf-8')
    if INJECT_BEGIN in text:
        sys.exit('已存在注入条目，先 restore')
    # 在 JSON 表开头插入（key 任意位置合法）
    marker = 'R"mdx({'
    i = text.index(marker) + len(marker)
    entries = ''.join(f'"{h}":{{{VOC_FT_PARAMS}}},' for h in md5s)
    entry = INJECT_BEGIN + entries + INJECT_END
    cpp.write_text(text[:i] + entry + text[i:], encoding='utf-8')
    print(f'已注入 {len(md5s)} 条（临时，勿提交）→ {cpp}')


def do_restore(cpp: Path):
    text = cpp.read_text(encoding='utf-8')
    i = text.find(INJECT_BEGIN)
    if i < 0:
        sys.exit('无注入条目')
    j = text.index(INJECT_END) + len(INJECT_END)
    cpp.write_text(text[:i] + text[j:], encoding='utf-8')
    print(f'已还原 → {cpp}')


def main():
    args = sys.argv[1:]
    check = '--check' in args
    args = [a for a in args if a != '--check']
    if not args:
        sys.exit(__doc__)
    cmd = args[0]
    if cmd == 'make':
        src, outdir = Path(args[1]), Path(args[2])
        if check:
            assert src.is_file(), f'缺 {src}'
            print(f'CHECK OK: make {src} → {outdir}（构造 bad-layout / bad-dtype 两模型）')
            return
        outdir.mkdir(parents=True, exist_ok=True)
        do_make(src, outdir)
    elif cmd == 'inject':
        cpp, md5s = Path(args[1]), args[2:]
        if check:
            assert cpp.is_file() and md5s
            for md5 in md5s:
                assert len(md5) == 32 and all(c in '0123456789abcdef' for c in md5), md5
            print(f'CHECK OK: inject {cpp} {" ".join(md5s)}')
            return
        do_inject(cpp, md5s)
    elif cmd == 'restore':
        cpp = Path(args[1])
        if check:
            assert cpp.is_file()
            print(f'CHECK OK: restore {cpp}')
            return
        do_restore(cpp)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()

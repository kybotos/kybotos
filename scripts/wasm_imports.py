#!/usr/bin/env python3
"""scripts/wasm_imports.py — アプリ × Host API のカバレッジ表(Phase 22)。

.wasm の import / export セクションを直接読み、shared/hostapi_defs.h の HOSTAPI_NATIVE_SYMBOLS にある
全関数(と任意 export の app_key / app_exit)について、どのアプリが使っているかを Markdown の表で出す。
回帰アプリを入れ替えるときに「どの API が回帰で触られなくなるか」を確かめるのに使う
(Phase 12 のカバレッジ表の作り直し。docs/results/phase22.md ステップ 0)。

import しているだけで、実際に呼ばれるかは分からない(タップしないと呼ばれない経路など)。

usage: python3 scripts/wasm_imports.py APP.wasm [APP.wasm ...]
       例: python3 scripts/wasm_imports.py wasm-apps/*/*.wasm wasm-apps/dev/*/*.wasm
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPTIONAL_EXPORTS = ['app_key', 'app_exit']


def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            return r, i


def imports_exports(path):
    b = open(path, 'rb').read()
    i = 8
    imp, exp = set(), set()
    while i < len(b):
        sid = b[i]
        i += 1
        n, i = leb(b, i)
        start = i
        if sid in (2, 7):  # import / export
            cnt, j = leb(b, i)
            for _ in range(cnt):
                if sid == 2:  # module 名
                    l, j = leb(b, j)
                    j += l
                l, j = leb(b, j)
                name = b[j:j + l].decode()
                j += l
                kind = b[j]
                j += 1
                if sid == 2 and kind != 0:
                    raise SystemExit(f'{path}: unsupported import kind {kind}')
                _, j = leb(b, j)
                (imp if sid == 2 else exp).add(name)
        i = start + n
    return imp, exp


def host_api():
    text = open(os.path.join(REPO, 'shared', 'hostapi_defs.h'), encoding='utf-8').read()
    m = text[text.index('#define HOSTAPI_NATIVE_SYMBOLS'):]
    m = m[:m.index('\n\n')]
    return re.findall(r'X\((hostapi_[a-z0-9_]+)', m)


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    apps = [(os.path.splitext(os.path.basename(p))[0], *imports_exports(p)) for p in paths]
    names = [a[0] for a in apps]
    print('| 関数 | ' + ' | '.join(names) + ' | 使うアプリの数 |')
    print('|---|' + '---|' * (len(names) + 1))
    for f in host_api() + OPTIONAL_EXPORTS:
        row = ['X' if (f in imp or f in exp) else '.' for _, imp, exp in apps]
        used = row.count('X')
        label = f.replace('hostapi_', '') + ('(export)' if f in OPTIONAL_EXPORTS else '')
        print(f'| `{label}` | ' + ' | '.join(row) + f' | {used if used else "**0**"} |')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

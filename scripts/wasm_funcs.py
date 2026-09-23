#!/usr/bin/env python3
"""scripts/wasm_funcs.py — .wasm のセクションの大きさと、大きい関数本体の一覧(Phase 21d)。

名前は name セクションから引く。アプリの release ビルドは strip しているので、名前が要るときは
`CARGO_PROFILE_RELEASE_STRIP=false cargo build --release --target-dir <別ディレクトリ>` で作ったものを渡す。
usage: python3 scripts/wasm_funcs.py APP.wasm [TOP]"""
import sys


def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            return r, i


def main(path, top=25):
    b = open(path, 'rb').read()
    i = 8
    imports = 0
    names = {}
    bodies = []
    secs = []
    while i < len(b):
        sid = b[i]
        i += 1
        n, i = leb(b, i)
        start = i
        secs.append((sid, n))
        if sid == 2:  # import
            cnt, j = leb(b, i)
            for _ in range(cnt):
                l, j = leb(b, j); j += l
                l, j = leb(b, j); j += l
                kind = b[j]; j += 1
                if kind == 0:
                    _, j = leb(b, j); imports += 1
                elif kind == 1:
                    j += 1; fl, j = leb(b, j); _, j = leb(b, j)
                    if fl & 1: _, j = leb(b, j)
                elif kind == 2:
                    fl, j = leb(b, j); _, j = leb(b, j)
                    if fl & 1: _, j = leb(b, j)
                elif kind == 3:
                    j += 2
        elif sid == 10:
            cnt, j = leb(b, i)
            for k in range(cnt):
                sz, j2 = leb(b, j)
                bodies.append((sz, imports + k))
                j = j2 + sz
        elif sid == 0:
            l, j = leb(b, i)
            nm = b[j:j + l].decode(errors='replace')
            j += l
            if nm == 'name':
                while j < start + n:
                    sub = b[j]; j += 1
                    sl, j = leb(b, j)
                    end = j + sl
                    if sub == 1:
                        c, j = leb(b, j)
                        for _ in range(c):
                            idx, j = leb(b, j)
                            ln, j = leb(b, j)
                            names[idx] = b[j:j + ln].decode(errors='replace')
                            j += ln
                    j = end
        i = start + n
    print('sections:', ', '.join(f'{sid}:{n}' for sid, n in secs))
    print('functions:', len(bodies), 'code total:', sum(s for s, _ in bodies))
    for sz, idx in sorted(bodies, reverse=True)[:top]:
        print(f'{sz:6d}  f{idx} {names.get(idx, "")}')


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 25)

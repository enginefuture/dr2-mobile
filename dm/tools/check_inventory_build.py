#!/usr/bin/env python3
"""Verify the iOS compact-inventory rewriter output before it is embedded."""
import argparse
import hashlib
from pathlib import Path
import sys

# Host gates for both grid icon draw calls, equipped dimensions, and socket limits.
REQUIRED = {(0x3b2124, 0x8f), (0x3b2198, 0x8f), (0x3cf2f8, 0x91),
            (0x575528, 0x90), (0x5756e8, 0x90), (0x56ba60, 0x90)}


def check(out, script):
    out = Path(out)
    try:
        expected = hashlib.sha256(Path(script).read_bytes()).hexdigest()
        recorded = (out / 'build_src.sha256').read_text().split()[0]
        if recorded != expected:
            return 'patch_nso.py 指纹不一致 / patch rewriter fingerprint mismatch'
        for mod in ('main', 'sdk'):
            for suffix in ('patched.bin', 'fixups.tsv', 'gates.tsv'):
                if not (out / f'{mod}.{suffix}').is_file():
                    return f'缺少产物 / missing output: {mod}.{suffix}'
        found = {}
        for line in (out / 'main.gates.tsv').read_text().splitlines():
            row = line.split('\t')
            if len(row) >= 4 and row[1] == 'svc':
                found[(int(row[3], 0), int(row[2], 0))] = int(row[0], 0)
        missing = REQUIRED - found.keys()
        if missing:
            return '缺少背包保护门 / missing inventory gates: ' + ', '.join(hex(addr) for addr, _ in sorted(missing))
        with (out / 'main.patched.bin').open('rb') as image:
            for addr, svc in REQUIRED:
                image.seek(addr)
                raw = image.read(4)
                if len(raw) != 4:
                    return f'背包补丁位置缺失 / missing patch at {addr:#x}'
                op = int.from_bytes(raw, 'little')
                imm = op & 0x3ffffff
                if imm & 0x2000000:
                    imm -= 0x4000000
                if (op & 0xfc000000) != 0x14000000 or addr + imm * 4 != found[(addr, svc)] - 0x20:
                    return f'背包跳转与门表不匹配 / inventory jump mismatch at {addr:#x}'
    except (OSError, IndexError, ValueError) as exc:
        return f'产物无法核验 / cannot verify outputs: {exc}'
    return None


def check_footprints(path):
    seen = set()
    try:
        with Path(path).open(encoding='utf-8') as f:
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) != 3:
                    return 'invalid inventory footprint row'
                item, width, height = map(int, parts)
                if item in seen or not (0 <= item < 1024 and 1 <= width <= 8 and 1 <= height <= 8):
                    return 'invalid or duplicate inventory footprint'
                seen.add(item)
    except (OSError, ValueError) as exc:
        return f'cannot verify inventory footprints: {exc}'
    return None if seen else 'empty inventory footprints'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('out', nargs='?')
    p.add_argument('--footprints')
    p.add_argument('--script', default=str(Path(__file__).with_name('patch_nso.py')))
    args = p.parse_args()
    if bool(args.out) == bool(args.footprints):
        p.error("supply either output directory or --footprints")
    problem = check(args.out, args.script) if args.out else check_footprints(args.footprints)
    if problem:
        print(problem, file=sys.stderr)
        return 1
    print('背包校验通过 / inventory verification passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())

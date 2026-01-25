#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


def dhash_rgba8(img: Image.Image) -> int:
    rgba = img.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()

    g = [0] * (9 * 8)
    for y in range(8):
        for x in range(9):
            sx = int(((x + 0.5) * w) / 9.0)
            sy = int(((y + 0.5) * h) / 8.0)
            if sx < 0:
                sx = 0
            if sy < 0:
                sy = 0
            if sx >= w:
                sx = w - 1
            if sy >= h:
                sy = h - 1
            r, gg, b, _a = px[sx, sy]
            gray = (r * 77 + gg * 150 + b * 29) >> 8
            g[y * 9 + x] = gray

    out = 0
    bit = 0
    for y in range(8):
        for x in range(8):
            a = g[y * 9 + x]
            b = g[y * 9 + x + 1]
            if a > b:
                out |= 1 << bit
            bit += 1
    return out


def popcount64(v: int) -> int:
    return int(v & ((1 << 64) - 1)).bit_count()


def load_dhash(path: Path) -> int:
    s = path.read_text(encoding="utf-8").strip()
    if not s:
        return 0
    return int(s, 16)


def write_dhash(path: Path, h: int) -> None:
    path.write_text(f"{h:016x}\n", encoding="utf-8")


@dataclass(frozen=True)
class CompareResult:
    ok: bool
    dist: int
    got: int
    want: int


def compare(got: int, want: int, tol: int) -> CompareResult:
    dist = popcount64(got ^ want)
    return CompareResult(ok=(dist <= tol), dist=dist, got=got, want=want)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("png", type=Path)
    ap.add_argument("--write", type=Path, default=None, help="Write computed dhash to this path")
    ap.add_argument("--baseline", type=Path, default=None, help="Baseline .dhash to compare against")
    ap.add_argument("--tolerance", type=int, default=8)
    args = ap.parse_args()

    img = Image.open(args.png)
    h = dhash_rgba8(img)

    if args.write is not None:
        args.write.parent.mkdir(parents=True, exist_ok=True)
        write_dhash(args.write, h)

    if args.baseline is None:
        print(f"{h:016x}")
        return 0

    want = load_dhash(args.baseline)
    res = compare(h, want, args.tolerance)
    if res.ok:
        print(f"[dhash] OK dist={res.dist} got={res.got:016x}")
        return 0
    print(f"[dhash] MISMATCH dist={res.dist} tol={args.tolerance} got={res.got:016x} want={res.want:016x}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())


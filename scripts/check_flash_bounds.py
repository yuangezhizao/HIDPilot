#!/usr/bin/env python3
"""Fail the firmware build if its flash image reaches the reserved configuration sectors."""

import argparse
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--limit", required=True, type=lambda value: int(value, 0))
    args = parser.parse_args()
    output = subprocess.run([args.nm, "-n", args.elf], check=True, capture_output=True, text=True).stdout
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] in {"__flash_binary_start", "__flash_binary_end"}:
            symbols[fields[-1]] = int(fields[0], 16)
    if "__flash_binary_end" not in symbols:
        raise SystemExit("未在 ELF 中找到 __flash_binary_end，无法验证 Flash 边界")
    start = symbols.get("__flash_binary_start", 0x10000000)
    end = symbols["__flash_binary_end"]
    if end > args.limit:
        raise SystemExit(f"固件 Flash 末地址 0x{end:08x} 越过配置区起点 0x{args.limit:08x}")
    print(f"HIDPilot Flash 边界通过：0x{start:08x}..0x{end:08x} < 0x{args.limit:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

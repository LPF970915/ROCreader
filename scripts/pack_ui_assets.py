#!/usr/bin/env python3
import pathlib
import struct
import sys


MAGIC = b"RCUIPK01"
KEY = b"ROCreader::native_h700::ui_pack"


def xor_stream(data: bytes, name: str) -> bytes:
    name_bytes = name.encode("utf-8")
    out = bytearray(len(data))
    for i, b in enumerate(data):
        k = KEY[i % len(KEY)]
        n = name_bytes[i % len(name_bytes)] if name_bytes else 0
        out[i] = b ^ k ^ n ^ ((i * 131) & 0xFF)
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: pack_ui_assets.py <ui_dir> <output_pack>", file=sys.stderr)
        return 2

    ui_dir = pathlib.Path(sys.argv[1]).resolve()
    out_path = pathlib.Path(sys.argv[2]).resolve()
    if not ui_dir.is_dir():
        print(f"ui dir not found: {ui_dir}", file=sys.stderr)
        return 1

    files = []
    for path in sorted(ui_dir.iterdir(), key=lambda p: p.name.lower()):
        if not path.is_file():
            continue
        rel_name = path.name
        payload = path.read_bytes()
        files.append((rel_name, xor_stream(payload, rel_name), len(payload)))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", len(files)))
        for name, encrypted, original_size in files:
            name_bytes = name.encode("utf-8")
            f.write(struct.pack("<H", len(name_bytes)))
            f.write(name_bytes)
            f.write(struct.pack("<I", original_size))
            f.write(struct.pack("<I", len(encrypted)))
            f.write(encrypted)

    print(f"packed {len(files)} ui assets -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

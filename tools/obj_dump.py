#!/usr/bin/env python3
"""
Lightweight objdump for the custom MyLinker object format.
Shows header, text/data bytes, symbols, and relocations.
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x4C4E4B31  # "LNK1"

SECTION_NAMES = {
    0: "TEXT",
    1: "DATA",
}

SYMBOL_TYPES = {
    0: "UNDEF",
    1: "DEF",
}

RELOC_TYPES = {
    0: "ABS",
    1: "REL",
}


def read_cstring(raw: bytes) -> str:
    """Decode a fixed-length, NUL-terminated string field."""
    try:
        return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")
    except Exception:
        return ""


def hexdump(data: bytes, base: int = 0, width: int = 16):
    """Yield hexdump lines for the given byte sequence."""
    for i in range(0, len(data), width):
        chunk = data[i : i + width]
        hex_bytes = " ".join(f"{b:02x}" for b in chunk)
        ascii_repr = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        yield f"{base + i:08x}: {hex_bytes:<{width*3}} |{ascii_repr}|"


def parse_obj(path: Path):
    buf = path.read_bytes()
    hdr_size = struct.calcsize("<IIIII")
    if len(buf) < hdr_size:
        raise ValueError("File too small to contain header")

    magic, text_size, data_size, sym_cnt, reloc_cnt = struct.unpack_from(
        "<IIIII", buf, 0
    )
    if magic != MAGIC:
        raise ValueError(f"Bad magic 0x{magic:08x} (expected 0x{MAGIC:08x})")

    off = hdr_size
    text = buf[off : off + text_size]
    off += text_size
    data_sec = buf[off : off + data_size]
    off += data_size

    syms = []
    sym_struct = struct.Struct("<64sIII")
    for _ in range(sym_cnt):
        if off + sym_struct.size > len(buf):
            raise ValueError("Truncated symbol table")
        raw = sym_struct.unpack_from(buf, off)
        syms.append(
            {
                "name": read_cstring(raw[0]),
                "type": raw[1],
                "section": raw[2],
                "offset": raw[3],
            }
        )
        off += sym_struct.size

    relocs = []
    reloc_struct = struct.Struct("<I64sI")
    for _ in range(reloc_cnt):
        if off + reloc_struct.size > len(buf):
            raise ValueError("Truncated relocation table")
        raw = reloc_struct.unpack_from(buf, off)
        relocs.append(
            {
                "offset": raw[0],
                "symbol_name": read_cstring(raw[1]),
                "type": raw[2],
            }
        )
        off += reloc_struct.size

    return {
        "text": text,
        "data": data_sec,
        "symbols": syms,
        "relocs": relocs,
        "header": {
            "text_size": text_size,
            "data_size": data_size,
            "sym_cnt": sym_cnt,
            "reloc_cnt": reloc_cnt,
        },
    }


def dump_obj(path: Path, args):
    print(f"== {path} ==")
    try:
        obj = parse_obj(path)
    except Exception as e:
        print(f"ERROR: {e}")
        return

    hdr = obj["header"]
    print(
        f"Header: text={hdr['text_size']} bytes, data={hdr['data_size']} bytes, "
        f"symbols={hdr['sym_cnt']}, relocs={hdr['reloc_cnt']}"
    )

    if obj["text"]:
        print(f"\n.text ({len(obj['text'])} bytes)")
        for line in hexdump(obj["text"], base=0, width=args.width):
            print(f"  {line}")
    if obj["data"]:
        print(f"\n.data ({len(obj['data'])} bytes)")
        for line in hexdump(obj["data"], base=0, width=args.width):
            print(f"  {line}")

    if obj["symbols"]:
        print("\nSymbols:")
        for idx, s in enumerate(obj["symbols"]):
            stype = SYMBOL_TYPES.get(s["type"], str(s["type"]))
            sect = SECTION_NAMES.get(s["section"], str(s["section"]))
            print(
                f"  [{idx:02d}] {s['name']:<20} type={stype:<5} section={sect:<4} offset=0x{s['offset']:x}"
            )

    if obj["relocs"]:
        print("\nRelocations:")
        for idx, r in enumerate(obj["relocs"]):
            rtype = RELOC_TYPES.get(r["type"], str(r["type"]))
            print(
                f"  [{idx:02d}] offset=0x{r['offset']:x} type={rtype:<3} symbol={r['symbol_name']}"
            )
    print()


def main(argv):
    ap = argparse.ArgumentParser(
        description="objdump for MyLinker object files (LNK1 format)"
    )
    ap.add_argument("files", nargs="+", type=Path, help="object file(s) to dump")
    ap.add_argument(
        "--width", type=int, default=16, help="bytes per hexdump line (default: 16)"
    )
    args = ap.parse_args(argv)

    for f in args.files:
        dump_obj(f, args)


if __name__ == "__main__":
    main(sys.argv[1:])

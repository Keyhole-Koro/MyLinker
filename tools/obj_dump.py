#!/usr/bin/env python3
"""
Lightweight objdump for the custom MyLinker object format.
Shows header, text/data bytes, symbols, and relocations.
"""
import argparse
import sys
from pathlib import Path

from objformat import (
    COLLECT_ENTRY,
    FILE_HEADER,
    LINKER_MAGIC,
    RELOC_ABSOLUTE,
    RELOC_ENTRY,
    RELOC_RELATIVE,
    RELOC_WORD32,
    SECTION_NAMES,
    SYMBOL_DEFINED,
    SYMBOL_ENTRY,
    SYMBOL_UNDEFINED,
    cstring,
)

SYMBOL_TYPES = {
    SYMBOL_UNDEFINED: "UNDEF",
    SYMBOL_DEFINED: "DEF",
}

RELOC_TYPES = {
    RELOC_ABSOLUTE: "ABS",
    RELOC_RELATIVE: "REL",
    RELOC_WORD32: "WORD32",
}


def hexdump(data: bytes, base: int = 0, width: int = 16):
    """Yield hexdump lines for the given byte sequence."""
    for i in range(0, len(data), width):
        chunk = data[i : i + width]
        hex_bytes = " ".join(f"{b:02x}" for b in chunk)
        ascii_repr = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        yield f"{base + i:08x}: {hex_bytes:<{width*3}} |{ascii_repr}|"


def parse_obj(path: Path):
    buf = path.read_bytes()
    if len(buf) < FILE_HEADER.size:
        raise ValueError("File too small to contain header")

    magic, text_size, data_size, sym_cnt, reloc_cnt, collect_cnt, collect_size = FILE_HEADER.unpack_from(
        buf, 0
    )
    if magic != LINKER_MAGIC:
        raise ValueError(f"Bad magic 0x{magic:08x} (expected 0x{LINKER_MAGIC:08x})")

    off = FILE_HEADER.size
    text = buf[off : off + text_size]
    off += text_size
    data_sec = buf[off : off + data_size]
    off += data_size
    blob = buf[off : off + collect_size]
    off += collect_size

    syms = []
    for _ in range(sym_cnt):
        if off + SYMBOL_ENTRY.size > len(buf):
            raise ValueError("Truncated symbol table")
        raw = SYMBOL_ENTRY.unpack_from(buf, off)
        syms.append(
            {
                "name": cstring(raw[0]),
                "type": raw[1],
                "section": raw[2],
                "offset": raw[3],
            }
        )
        off += SYMBOL_ENTRY.size

    relocs = []
    for _ in range(reloc_cnt):
        if off + RELOC_ENTRY.size > len(buf):
            raise ValueError("Truncated relocation table")
        raw = RELOC_ENTRY.unpack_from(buf, off)
        relocs.append(
            {
                "offset": raw[0],
                "symbol_name": cstring(raw[1]),
                "type": raw[2],
                "section": raw[3],
            }
        )
        off += RELOC_ENTRY.size

    collects = []
    for _ in range(collect_cnt):
        if off + COLLECT_ENTRY.size > len(buf):
            raise ValueError("Truncated collected-section table")
        raw = COLLECT_ENTRY.unpack_from(buf, off)
        collects.append({"name": cstring(raw[0]), "offset": raw[1], "size": raw[2]})
        off += COLLECT_ENTRY.size

    return {
        "text": text,
        "data": data_sec,
        "symbols": syms,
        "relocs": relocs,
        "collects": collects,
        "blob": blob,
        "header": {
            "text_size": text_size,
            "data_size": data_size,
            "sym_cnt": sym_cnt,
            "reloc_cnt": reloc_cnt,
            "collect_cnt": collect_cnt,
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

    if obj["collects"]:
        print("Collected sections:")
        for idx, c in enumerate(obj["collects"]):
            print(f"  [{idx}] {c['name']}: blob+0x{c['offset']:x}, {c['size']} bytes")
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
        description="objdump for MyLinker object files (LNK3 format)"
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

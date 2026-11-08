"""The LNK3 object format for Python tools.

inc/ObjectFormat.h is the definition; this module repeats its object-file
constants and record layouts so that the tools reading and writing objects
(obj_gen.py, obj_dump.py, the root repository's qa/tools/obj-viewer.py) share
one copy. Keep the two in step.

An object is a FileHeader, the text, data and collected-section bytes, then
the symbol, relocation and collected-section tables, and optionally a debug
trailer. The header and tables are little-endian and packed.
"""

import struct

LINKER_MAGIC = 0x4C4E4B33  # "LNK3"
DEBUG_MAGIC = 0x4D444247   # "MDBG"

# SymbolEntry.section and RelocEntry.section
SECTION_TEXT = 0
SECTION_DATA = 1
SECTION_COLLECT = 2  # the object's collected-section blob

# SymbolEntry.type
SYMBOL_UNDEFINED = 0
SYMBOL_DEFINED = 1

# RelocEntry.type
RELOC_ABSOLUTE = 0  # 21-bit immediate of `movi rX, label`
RELOC_RELATIVE = 1  # 26-bit pc-relative offset of a call or jump
RELOC_WORD32 = 2    # a whole data word (`.word label`)

# A label the assembler makes private to its object is named
# `<label>-[<module>:<n>]` (PRIVATE_LABEL_FORMAT); the mark identifies one.
PRIVATE_LABEL_MARK = "-["

SECTION_NAMES = {SECTION_TEXT: "TEXT", SECTION_DATA: "DATA", SECTION_COLLECT: "COLLECT"}
SYMBOL_TYPE_NAMES = {SYMBOL_UNDEFINED: "UNDEFINED", SYMBOL_DEFINED: "DEFINED"}
RELOC_TYPE_NAMES = {RELOC_ABSOLUTE: "ABSOLUTE", RELOC_RELATIVE: "RELATIVE", RELOC_WORD32: "WORD32"}

# magic, text_size, data_size, symtable_count, reloc_count, collect_count, collect_size
FILE_HEADER = struct.Struct("<7I")
# name[64], type, section, offset
SYMBOL_ENTRY = struct.Struct("<64sIII")
# offset, symbol_name[64], type, section
RELOC_ENTRY = struct.Struct("<I64sII")
# name[64], offset, size
COLLECT_ENTRY = struct.Struct("<64sII")
# magic, string_size, func_count, row_count; then the strings, the funcs and the rows
DEBUG_HEADER = struct.Struct("<4I")
# offset, name, file
DEBUG_FUNC = struct.Struct("<3I")
# offset, file, line, cost
DEBUG_ROW = struct.Struct("<4I")


def cstring(raw: bytes) -> str:
    """Decode a fixed-size, NUL-padded name field."""
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def read_debug_trailer(buf: bytes, off: int):
    """The debug trailer at `off`, just after the collected-section table, or
    None if the object has none. Returns (funcs, rows) with the strings looked
    up: a func is (offset, name, file), a row (offset, file, line, cost), its
    cost "" outside a cost region."""
    if off + DEBUG_HEADER.size > len(buf):
        return None
    magic, string_size, func_count, row_count = DEBUG_HEADER.unpack_from(buf, off)
    if magic != DEBUG_MAGIC:
        return None
    off += DEBUG_HEADER.size
    end = off + string_size + func_count * DEBUG_FUNC.size + row_count * DEBUG_ROW.size
    if end > len(buf):
        raise ValueError("Truncated debug trailer")
    strings = buf[off:off + string_size]
    off += string_size
    funcs = []
    for _ in range(func_count):
        offset, name, file = DEBUG_FUNC.unpack_from(buf, off)
        funcs.append((offset, cstring(strings[name:]), cstring(strings[file:])))
        off += DEBUG_FUNC.size
    rows = []
    for _ in range(row_count):
        offset, file, line, cost = DEBUG_ROW.unpack_from(buf, off)
        rows.append((offset, cstring(strings[file:]), line, cstring(strings[cost:])))
        off += DEBUG_ROW.size
    return funcs, rows

#!/usr/bin/env python3
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = REPO_ROOT / "test"
LINKER_EXE = REPO_ROOT / "mllinker"
OBJ_GEN = REPO_ROOT / "tools" / "obj_gen.py"

sys.path.insert(0, str(REPO_ROOT / "tools"))
from objformat import (COLLECT_ENTRY, DEBUG_HEADER, DEBUG_MAGIC, FILE_HEADER,
                       LINKER_MAGIC, RELOC_ENTRY, SYMBOL_ENTRY)


def raw_object(text=b"", data=b"", blob=b"", symbols=(), relocs=(), collects=(),
               header=None, trailer=b""):
    """The bytes of an object, for a malformed one obj_gen.py cannot write.
    `symbols`, `relocs` and `collects` are the fields of their records, in
    objformat's order; `header` replaces the counted FileHeader fields and
    `trailer` follows the tables (a debug trailer, say). Slice the result to
    cut the object short."""
    if header is None:
        header = (LINKER_MAGIC, len(text), len(data), len(symbols), len(relocs),
                  len(collects), len(blob))
    out = FILE_HEADER.pack(*header) + text + data + blob
    out += b"".join(SYMBOL_ENTRY.pack(*sym) for sym in symbols)
    out += b"".join(RELOC_ENTRY.pack(*reloc) for reloc in relocs)
    out += b"".join(COLLECT_ENTRY.pack(*ce) for ce in collects)
    return out + trailer


# The pieces of a minimal object: `halt` at __START__.
HALT = bytes([252, 0, 0, 0])
START = (b"__START__", 1, 0, 0)
HALT_OBJECT = raw_object(text=HALT, symbols=[START])


# (name, json inputs, should_link, optional linker flags, optional expected word)
TESTS = [
    ("test_basic", ["test_A.json", "test_B.json"], True, [], None),
    # Verifies the linker synthesizes the `_end` symbol; without it this object's
    # undefined `_end` reference would fail to resolve.
    ("test_end_symbol", ["test_end_symbol.json"], True, [], None),
    # Two objects defining the same generic instantiation, the way every object
    # that uses a template emits its own copy. The first definition stands for
    # all of them instead of being a duplicate-symbol error.
    ("test_mlg_dup", ["test_mlg_dup_A.json", "test_mlg_dup_B.json"], True, [], None),
    # The same collision on an ordinary name is still an error: only the
    # compiler-owned __mlg_ prefix is mergeable.
    ("test_plain_dup", ["test_plain_dup_A.json", "test_plain_dup_B.json"], False, [], None),
    # `real` is at 0x8 and `mock_entry` at 0xC. The CALL relocation at 0x4
    # must therefore become +8 rather than its original +4 target.
    ("test_redirect", ["test_redirect_A.json", "test_redirect_B.json"], True,
     ["--redirect", "real=mock_entry"], (4, 0x6C000008)),
    # Two objects each contribute a chunk to the collected section `rows`.
    # The chunks are laid out contiguously after the data (text is 8 bytes):
    # A's [11, 12] then B's [21, 22, 23], then the size word 20. The word at
    # 8 + 8 (B's first) reads 21. B is kept although nothing references it:
    # contributing to a section is what makes an object live.
    ("test_collect", ["test_collect_A.json", "test_collect_B.json"], True, [], (16, 0x00000015)),
    # Same link, the section directory: after the 20 chunk bytes (at 28) comes
    # the row [name, start, size] = [52, 8, 20], a zero row at 40, "rows\0" at
    # 52. The size word at 36 reads 20; `__section_rows_size` names that word.
    ("test_collect_dir", ["test_collect_A.json", "test_collect_B.json"], True, [], (36, 0x00000014)),
    # Both objects have a private `helper` (SYMBOL_SHARED) and each refers to
    # its own by its private label: no duplicate. A's movi gets its own, at 8.
    ("test_shared", ["test_shared_A.json", "test_shared_B.json"], True, [], (4, 0x08200008)),
    # The same, but A names `helper` itself: two modules share the name, so
    # nothing says which one is meant.
    ("test_shared_named", ["test_shared_named_A.json", "test_shared_B.json"], False, [], None),
    # `movi r1, target` assembles to LUI and ADDIS. Linked at 0x40000000,
    # `target` is 0x40000008: LUI takes 0x40000008 >> 11 = 0x80000 and ADDIS
    # its low 11 bits, 8.
    ("test_split_address_hi", ["test_split_address.json"], True, ["--base", "40000000"], (0, 0x94280000)),
    ("test_split_address_lo", ["test_split_address.json"], True, ["--base", "40000000"], (4, 0x64200008)),
]

# Malformed objects the linker must refuse with an error naming the object:
# exit status 1, not a link and not a crash. (name, inputs, message, flags);
# the error is the message, then the path of the last object, the malformed
# one. An input is a JSON fixture or, for an object obj_gen.py cannot write,
# a (file name, raw_object bytes) pair.
REJECTED = [
    # A relocation at offset 0xFFFFFFFC, in 8 bytes of text: offset + 4 wraps
    # around to 0, which a check of that sum lets through to patch far
    # outside the text.
    ("test_reloc_offset_wraps", ["test_reloc_offset_wraps.json"],
     "Error: Relocation offset out of bounds in", []),
    # Relocation type 9 is none of ObjRelocType. An unknown type used to be
    # patched as an ABSOLUTE to address 0: `movi r1, target` linked as
    # `movi r1, 0`.
    ("test_unknown_reloc_type", ["test_unknown_reloc_type.json"],
     "Error: Unknown relocation type 9 for 'target' in", []),
    # Objects cut short, or whose header counts more than the file holds. A
    # failed read used to go unnoticed: the rest of the header stayed
    # uninitialized, a missing table read as zeros -- a collected-section
    # chunk with an empty name -- and a huge size was allocated before
    # anything was read.
    ("test_header_cut", [("header_cut.obj", HALT_OBJECT[:4])],
     "Error: Truncated header in", []),
    ("test_header_only", [("header_only.obj", HALT_OBJECT[:FILE_HEADER.size])],
     "Error: Truncated text section in", []),
    ("test_text_size_huge",
     [("text_size_huge.obj", raw_object(text=HALT, symbols=[START],
                                        header=(LINKER_MAGIC, 0xFFFFFFF0, 0, 1, 0, 0, 0)))],
     "Error: Truncated text section in", []),
    ("test_symbol_count_huge",
     [("symbol_count_huge.obj", raw_object(text=HALT, symbols=[START],
                                           header=(LINKER_MAGIC, 4, 0, 0xFFFFFFFF, 0, 0, 0)))],
     "Error: Truncated symbol table in", []),
    ("test_collect_table_cut",
     [("collect_table_cut.obj", raw_object(text=HALT, blob=bytes([1, 2, 3, 4]), symbols=[START],
                                           collects=[(b"rows", 0, 4)])[:-COLLECT_ENTRY.size])],
     "Error: Truncated collected-section table in", []),
    ("test_debug_strings_huge",
     [("debug_strings_huge.obj",
       raw_object(text=HALT, symbols=[START], trailer=DEBUG_HEADER.pack(DEBUG_MAGIC, 0xFFFFFFF0, 0, 0)))],
     "Error: Truncated debug trailer in", []),
    # A 64-character name fills its field with no NUL after it; read as a C
    # string it ran on into the next field.
    ("test_unterminated_name", ["test_unterminated_name.json"],
     "Error: Unterminated symbol name in", []),
]

# `helper` is a function nothing references, so it is not in the relocation
# table; the map must still name it. `b_loop-[a:1]` is a block label and stays
# out, or it would split `helper` in two for a reader of the map.
MAP_TEST = ("test_map", ["test_map_A.json"], ["0x00000000 __START__", "0x00000004 helper"], ["b_loop"])

# A whole MBIN image and its map, byte for byte: text and page-aligned data
# from two objects, their collected-section chunks laid out by name with the
# directory after them, all three relocation types in every section that may
# hold them, and C dropped because nothing needs it. After an intended change
# to the layout, regenerate test_image.bin and test_image.map from this link.
IMAGE_TEST = ("test_image", ["test_image_A.json", "test_image_B.json", "test_image_C.json"],
              ["--header", "--base", "1000"])


def colored(text, color_code):
    return f"\033[{color_code}m{text}\033[0m"


def status_line(label, message, color="36"):
    print(colored(f"[{label}]", color), message)


def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True)


def build_objects(inputs, temp_root):
    """Write each input's object into temp_root and return their paths: a
    JSON fixture through obj_gen.py, a (file name, bytes) pair as is."""
    obj_files = []
    for item in inputs:
        if isinstance(item, str):
            output_obj = temp_root / item.replace(".json", ".obj")
            run(["python3", str(OBJ_GEN), str(TEST_DIR / item), str(output_obj)])
        else:
            file_name, content = item
            output_obj = temp_root / file_name
            output_obj.write_bytes(content)
        obj_files.append(str(output_obj))
    return obj_files


def build_linker():
    result = subprocess.run(["make", "-C", str(REPO_ROOT), "all"], check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    build_linker()
    passed = 0
    failed = 0
    temp_root = Path(tempfile.mkdtemp(prefix="mylinker-integration-tests-"))
    try:
        for name, json_inputs, should_link, flags, expected_word in TESTS:
            obj_files = build_objects(json_inputs, temp_root)
            output_bin = temp_root / f"{name}.bin"
            result = subprocess.run(
                [str(LINKER_EXE)] + flags + [str(output_bin)] + obj_files,
                capture_output=True, text=True,
            )
            if should_link and result.returncode != 0:
                status_line("FAIL", f"{name} did not link: {result.stderr.strip()}", "31")
                failed += 1
                continue
            if not should_link:
                if result.returncode == 0:
                    status_line("FAIL", f"{name} linked but should have been rejected", "31")
                    failed += 1
                    continue
                status_line("PASS", f"{name} rejected as expected", "32")
                passed += 1
                continue
            if expected_word is not None:
                offset, expected = expected_word
                output = output_bin.read_bytes()
                actual = int.from_bytes(output[offset:offset + 4], "big")
                if actual != expected:
                    status_line("FAIL", f"{name} expected 0x{expected:08X}, got 0x{actual:08X}", "31")
                    failed += 1
                    continue
            status_line("PASS", f"{name} linked successfully", "32")
            passed += 1

        for name, inputs, message, flags in REJECTED:
            obj_files = build_objects(inputs, temp_root)
            result = subprocess.run([str(LINKER_EXE)] + flags + [str(temp_root / f"{name}.bin")]
                                    + obj_files, capture_output=True, text=True)
            expected = f"{message} {obj_files[-1]}"
            if result.returncode != 1 or expected not in result.stderr:
                status_line("FAIL", f"{name} expected exit 1 and '{expected}', got exit "
                            f"{result.returncode}: {result.stderr.strip()}", "31")
                failed += 1
            else:
                status_line("PASS", f"{name} rejected with its error", "32")
                passed += 1

        name, json_inputs, wanted, unwanted = MAP_TEST
        obj_files = build_objects(json_inputs, temp_root)
        map_path = temp_root / f"{name}.map"
        run([str(LINKER_EXE), "--map", str(map_path), str(temp_root / f"{name}.bin")] + obj_files)
        lines = map_path.read_text().splitlines()
        missing = [line for line in wanted if line not in lines]
        leaked = [line for line in lines for label in unwanted if label in line]
        if missing or leaked:
            status_line("FAIL", f"{name} missing {missing}, unexpected {leaked}", "31")
            failed += 1
        else:
            status_line("PASS", f"{name} names unreferenced functions only", "32")
            passed += 1

        name, json_inputs, flags = IMAGE_TEST
        obj_files = build_objects(json_inputs, temp_root)
        output_bin = temp_root / f"{name}.bin"
        map_path = temp_root / f"{name}.map"
        run([str(LINKER_EXE)] + flags + ["--map", str(map_path), str(output_bin)] + obj_files)
        differ = [path.name for path, expected in ((output_bin, TEST_DIR / f"{name}.bin"),
                                                   (map_path, TEST_DIR / f"{name}.map"))
                  if path.read_bytes() != expected.read_bytes()]
        if differ:
            status_line("FAIL", f"{name} differs from the expected {', '.join(differ)}", "31")
            failed += 1
        else:
            status_line("PASS", f"{name} image and map match byte for byte", "32")
            passed += 1
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)
    status_line("DONE", f"Summary: {passed} passed, {failed} failed", "32" if failed == 0 else "31")
    raise SystemExit(1 if failed else 0)

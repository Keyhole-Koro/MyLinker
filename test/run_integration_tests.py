#!/usr/bin/env python3
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = REPO_ROOT / "test"
LINKER_EXE = REPO_ROOT / "mllinker"
OBJ_GEN = REPO_ROOT / "tools" / "obj_gen.py"
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
]

# Malformed objects the linker must refuse with an error naming the object:
# exit status 1, not a link and not a crash. (name, json inputs, message);
# the error is the message, then the path of the last object, the malformed
# one.
REJECTED = [
    # A relocation at offset 0xFFFFFFFC, in 8 bytes of text: offset + 4 wraps
    # around to 0, which a check of that sum lets through to patch far
    # outside the text.
    ("test_reloc_offset_wraps", ["test_reloc_offset_wraps.json"],
     "Error: Relocation offset out of bounds in"),
    # Relocation type 3 is none of ABSOLUTE, RELATIVE and WORD32. It used to
    # be patched as an ABSOLUTE to address 0: `movi r1, target` linked as
    # `movi r1, 0`.
    ("test_unknown_reloc_type", ["test_unknown_reloc_type.json"],
     "Error: Unknown relocation type 3 for 'target' in"),
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
            obj_files = []
            for json_file in json_inputs:
                input_path = TEST_DIR / json_file
                output_obj = temp_root / json_file.replace(".json", ".obj")
                run(["python3", str(OBJ_GEN), str(input_path), str(output_obj)])
                obj_files.append(str(output_obj))
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

        for name, json_inputs, message in REJECTED:
            obj_files = []
            for json_file in json_inputs:
                output_obj = temp_root / json_file.replace(".json", ".obj")
                run(["python3", str(OBJ_GEN), str(TEST_DIR / json_file), str(output_obj)])
                obj_files.append(str(output_obj))
            result = subprocess.run([str(LINKER_EXE), str(temp_root / f"{name}.bin")] + obj_files,
                                    capture_output=True, text=True)
            expected = f"{message} {obj_files[-1]}"
            if result.returncode != 1 or expected not in result.stderr:
                status_line("FAIL", f"{name} expected exit 1 and '{expected}', got exit "
                            f"{result.returncode}: {result.stderr.strip()}", "31")
                failed += 1
            else:
                status_line("PASS", f"{name} rejected with its error", "32")
                passed += 1

        name, json_inputs, wanted, unwanted = MAP_TEST
        obj_files = []
        for json_file in json_inputs:
            output_obj = temp_root / json_file.replace(".json", ".obj")
            run(["python3", str(OBJ_GEN), str(TEST_DIR / json_file), str(output_obj)])
            obj_files.append(str(output_obj))
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
        obj_files = []
        for json_file in json_inputs:
            output_obj = temp_root / json_file.replace(".json", ".obj")
            run(["python3", str(OBJ_GEN), str(TEST_DIR / json_file), str(output_obj)])
            obj_files.append(str(output_obj))
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

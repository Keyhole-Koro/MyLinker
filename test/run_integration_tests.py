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
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)
    status_line("DONE", f"Summary: {passed} passed, {failed} failed", "32" if failed == 0 else "31")
    raise SystemExit(1 if failed else 0)

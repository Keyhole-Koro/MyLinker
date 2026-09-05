#!/usr/bin/env python3
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = REPO_ROOT / "test"
LINKER_EXE = REPO_ROOT / "mllinker"
OBJ_GEN = REPO_ROOT / "tools" / "obj_gen.py"
# (name, json inputs, should_link)
TESTS = [
    ("test_basic", ["test_A.json", "test_B.json"], True),
    # Verifies the linker synthesizes the `_end` symbol; without it this object's
    # undefined `_end` reference would fail to resolve.
    ("test_end_symbol", ["test_end_symbol.json"], True),
    # Two objects defining the same generic instantiation, the way every object
    # that uses a template emits its own copy. The first definition stands for
    # all of them instead of being a duplicate-symbol error.
    ("test_mlg_dup", ["test_mlg_dup_A.json", "test_mlg_dup_B.json"], True),
    # The same collision on an ordinary name is still an error: only the
    # compiler-owned __mlg_ prefix is mergeable.
    ("test_plain_dup", ["test_plain_dup_A.json", "test_plain_dup_B.json"], False),
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
        for name, json_inputs, should_link in TESTS:
            obj_files = []
            for json_file in json_inputs:
                input_path = TEST_DIR / json_file
                output_obj = temp_root / json_file.replace(".json", ".obj")
                run(["python3", str(OBJ_GEN), str(input_path), str(output_obj)])
                obj_files.append(str(output_obj))
            output_bin = temp_root / f"{name}.bin"
            result = subprocess.run(
                [str(LINKER_EXE), str(output_bin)] + obj_files,
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
            status_line("PASS", f"{name} linked successfully", "32")
            passed += 1
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)
    status_line("DONE", f"Summary: {passed} passed, {failed} failed", "32" if failed == 0 else "31")
    raise SystemExit(1 if failed else 0)

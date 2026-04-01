#!/usr/bin/env python3
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = REPO_ROOT / "test"
LINKER_EXE = REPO_ROOT / "mllinker"
OBJ_GEN = REPO_ROOT / "tools" / "obj_gen.py"
TESTS = [
    ("test_basic", ["test_A.json", "test_B.json"]),
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
    temp_root = Path(tempfile.mkdtemp(prefix="mylinker-integration-tests-"))
    try:
        for name, json_inputs in TESTS:
            obj_files = []
            for json_file in json_inputs:
                input_path = TEST_DIR / json_file
                output_obj = temp_root / json_file.replace(".json", ".obj")
                run(["python3", str(OBJ_GEN), str(input_path), str(output_obj)])
                obj_files.append(str(output_obj))
            output_bin = temp_root / f"{name}.bin"
            run([str(LINKER_EXE), str(output_bin)] + obj_files)
            status_line("PASS", f"{name} linked successfully", "32")
            passed += 1
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)
    status_line("DONE", f"Summary: {passed} passed, 0 failed", "32")

# Minimal MyCCLinker

This is a minimal implementation of the `MyCCLinker` consistent with the design document.

## Structure
*   `inc/ObjectFormat.h`: Defines the `.obj` file format (Header, Sections, Symbols, Relocs).
*   `src/main.cpp`: The command line (`--map`, `--lines`, `--base`, `--header`, `--redirect`).
*   `src/Linker.cpp`: The linker implementation (C++).
*   `tools/obj_gen.py`: A helper script to generate `.obj` files from JSON (since Assembler support is pending).
*   `tools/objformat.py`: The format's constants and record layouts for the Python tools, repeated from `inc/ObjectFormat.h`.
*   `test/`: Sample JSON inputs and `run_integration_tests.py`, the test suite.

## How to Build
```bash
make
# or
g++ -std=c++17 -Iinc -o mllinker src/main.cpp src/Linker.cpp
```

## How to Test
```bash
make test-component
```
builds `mllinker`, generates objects from the JSON inputs in `test/` and checks
the links, the images and the maps, and that malformed objects and arguments are
refused.

To link by hand:

1.  **Generate Object Files:**
    Use the python script to create binary object files from the JSON descriptions.
    ```bash
    python3 tools/obj_gen.py test/test_A.json test/A.obj
    python3 tools/obj_gen.py test/test_B.json test/B.obj
    ```

2.  **Run Linker:**
    Link the object files into a single executable.
    ```bash
    ./mllinker program.bin test/A.obj test/B.obj
    ```

3.  **Verify Output:**
    The linker should report the text and data sizes.
    `A.obj` has 8 bytes text. `B.obj` has 4 bytes text. Total Text = 12 bytes.
    `funcA` is at 0x00. `funcB` is at 0x08.
    The relocation in `A.obj` (at offset 4) should be patched to point to `funcB`.
    ```bash
    hexdump -C program.bin
    ```
